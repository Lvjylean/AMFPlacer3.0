#!/usr/bin/env python3
"""Synthetic-only archive validation; never invokes the AMF placement entry point."""
import argparse
import csv
import datetime as dt
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import zipfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--fixture-config', type=Path, required=True,
                    help='Device/resource configuration; design input is replaced with four synthetic cells')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    build, out = args.build.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = {'state': 'running', 'created': dt.datetime.now(dt.timezone.utc).isoformat(),
              'scope': 'synthetic QP and four-cell diagnostic fixture; no placement flow',
              'build': str(build), 'commands': [], 'assertions': []}

    def check(ok, name):
        result['assertions'].append({'name': name, 'passed': bool(ok)})
        if not ok:
            raise AssertionError(name)

    def run(name, argv, success=True):
        start = time.monotonic()
        with (out / (name + '.log')).open('w') as log:
            proc = subprocess.run([str(x) for x in argv], stdout=log, stderr=subprocess.STDOUT)
        result['commands'].append({'name': name, 'argv': [str(x) for x in argv],
                                   'exit_code': proc.returncode, 'expected_success': success,
                                   'seconds': time.monotonic() - start})
        check((proc.returncode == 0) == success, name + ' exit status')

    def rows(path):
        with path.open() as f:
            return list(csv.DictReader(f, delimiter='\t', quoting=csv.QUOTE_NONE))

    try:
        fixture = '''curCell=> A type=> LUT6
   pin=> A/O refpin=> O dir=> OUT net=> n0 drivepin=> A/O
   pin=> A/I0 refpin=> I0 dir=> IN net=> n3 drivepin=> D/O
   pin=> A/I1 refpin=> I1 dir=> IN net=> clk drivepin=> C/O
   pin=> A/I2 refpin=> I2 dir=> IN net=> zero drivepin=> <const0>
curCell=> B type=> LUT6
   pin=> B/I0 refpin=> I0 dir=> IN net=> n0 drivepin=> A/O
   pin=> B/O refpin=> O dir=> OUT net=> n1 drivepin=> B/O
   pin=> B/I1 refpin=> I1 dir=> IN net=> clk drivepin=> C/O
curCell=> C type=> LUT6
   pin=> C/I0 refpin=> I0 dir=> IN net=> n1 drivepin=> B/O
   pin=> C/O refpin=> O dir=> OUT net=> clk drivepin=> C/O
curCell=> D type=> LUT6
   pin=> D/O refpin=> O dir=> OUT net=> n3 drivepin=> D/O
   pin=> D/I0 refpin=> I0 dir=> IN net=> zero drivepin=> <const0>
'''
        with zipfile.ZipFile(out / 'synthetic.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('synthetic.txt', fixture)
        cfg = json.loads(args.fixture_config.read_text())
        cfg['vivado extracted design information file'] = str(out / 'synthetic.zip')
        cfg['visualization'] = 'false'
        (out / 'fixture.json').write_text(json.dumps(cfg, indent=2) + '\n')
        result['device_inputs'] = {key: {'path': value, 'sha256': hashlib.sha256(Path(value).read_bytes()).hexdigest()}
                                   for key, value in cfg.items() if isinstance(value, str) and Path(value).is_file()}
        parse_cfg=dict(cfg,SllQpAuditQps='53;54;55',SllQuantityTau='10',SllQuantityGamma='0.02',y2xRatio='0.4')
        (out/'parse-config.json').write_text(json.dumps(parse_cfg,indent=2)+'\n')
        run('real-config-parser',[build/'checkFrozenQpAudit','--parse-config',out/'parse-config.json'])
        run('unit', [build / 'checkFrozenQpAudit', out / 'unit'])
        run('integration', [build / 'checkSllQpAuditIntegration', out / 'fixture.json', out / 'integration'])
        for name in ['checkSllQuantity', 'checkSllVisualizationDiagnostics', 'checkQPStability']:
            run(name, [build / name])
        for label, snapshot in [('unit', out / 'unit'),
                                ('integration', out / 'integration/enabled/frozen-qp/qp-1'),
                                ('baseline', out / 'integration/baseline_enabled/frozen-qp/qp-1')]:
            path = out / (label + '-replay.json')
            run(label + '-replay', [build / 'replayFrozenSllQp', snapshot, path, '24'])
            data = json.loads(path.read_text())
            check(data['coefficient'] == 24 and [v['tau'] for v in data['variants']] == [10, .2, .1],
                  label + ' coefficient and tau selection')
            check(data['model_gradient_error'] <= 1e-11, label + ' full model gradient reproduction')
            for variant in data['variants']:
                check(variant['relative_residual'] <= 1e-8, label + f" tau {variant['tau']} coupled residual")
                for i, target in enumerate(variant['targets']):
                    total = sum(net['target_contributions'][i] for net in variant['incident_net_contributions'])
                    check(abs(total - target['all_net_SLL_gradient']) <= 1e-10 * (1 + abs(total)),
                          label + f" tau {variant['tau']} PU {target['pu']} full incident-net sum")
            run(label + '-no-overwrite', [build / 'replayFrozenSllQp', snapshot, path, '24'], success=False)

        archive = out / 'integration/enabled/frozen-qp/qp-1'
        components = rows(archive / 'components.tsv')
        for pu in ['0', '1']:
            p = {r['component']: r for r in components if r['pu'] == pu}
            for axis in ['gx', 'gy']:
                summed = sum(float(v[axis]) for k, v in p.items()
                             if k not in ['original_total', 'sum_residual', 'sll_all_nets'])
                check(abs(summed - float(p['original_total'][axis])) < 1e-10,
                      f'PU {pu} {axis} component closure')
            check(abs(float(p['timing_slack_and_pairing']['gy']) - (1.25 if pu == '0' else 0)) < 1e-12,
                  f'PU {pu} timing edge vs anchor separation')
        nets = rows(archive / 'incident-nets.tsv')
        check({json.loads(r['net_json']) for r in nets} == {'A/O', 'B/O', 'C/O', 'D/O', '<const0>'},
              'All target incident nets inventoried, including excluded nets')
        pins = rows(archive / 'incident-pins.tsv')
        for net in nets:
            np = [p for p in pins if p['net_id'] == net['net_id']]
            if net['model_index'] != '-1':
                check(len(np) == int(net['pins']), 'All pins of incident model net ' + net['net_json'])
            else:
                check(net['pin_scope'] == 'target_member_pins_only_excluded_from_SLL' and
                      all(p['pu'] in ['0', '1'] for p in np), 'Explicit excluded-net scope ' + net['net_json'])
        for failure in ['budget_failure', 'marker_failure']:
            incomplete = out / ('integration/' + failure + '/frozen-qp/qp-1')
            check(not (incomplete / 'COMPLETE.json').exists(), failure + ' no completion marker')
            run(failure + '-reject', [build / 'replayFrozenSllQp', incomplete, out / (failure + '.json')], success=False)
        broken = out / 'truncated'
        shutil.copytree(archive, broken)
        with (broken / 'model.bin').open('r+b') as f:
            f.truncate(19)
        run('truncated-reject', [build / 'replayFrozenSllQp', broken, out / 'truncated.json'], success=False)
        result['state'] = 'passed'
    except Exception as exc:
        result['state'] = 'failed'
        result['error'] = str(exc)
        raise
    finally:
        result['finished'] = dt.datetime.now(dt.timezone.utc).isoformat()
        result['artifact_sha256'] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'summary.json'}
        result['test_binary_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                       for p in build.glob('*') if p.is_file() and
                                       (p.name.startswith('check') or p.name == 'replayFrozenSllQp')}
        (out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'state': result['state'], 'assertions': len(result['assertions']), 'output': str(out)}))


if __name__ == '__main__':
    main()
