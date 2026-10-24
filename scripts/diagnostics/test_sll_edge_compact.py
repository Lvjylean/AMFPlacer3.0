#!/usr/bin/env python3
"""Validate compact-edge exclusion with synthetic QPs, never a placement flow."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--fixture-config', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    r, b, e = a.root.resolve(), a.build.resolve(), a.output.resolve()
    e.mkdir(parents=True, exist_ok=False)
    result = dict(state='running', scope='synthetic tests only; no AMF placement or Vivado', commands=[], masked_replays=[])

    def run(name, args):
        start = time.monotonic()
        with (e/(name+'.log')).open('x') as f:
            p = subprocess.run([str(x) for x in args], stdout=f, stderr=subprocess.STDOUT)
        result['commands'].append(dict(name=name, argv=[str(x) for x in args], exit_code=p.returncode, wall_seconds=time.monotonic()-start))
        if p.returncode:
            raise RuntimeError(name+' failed')

    try:
        targets = ['checkSllQuantity', 'checkSllVisualizationDiagnostics', 'checkQPStability', 'checkFrozenQpAudit',
                   'replayFrozenSllQp', 'checkSllQpAuditIntegration', 'checkSllAdaptivePolicy', 'checkSllAdaptiveIntegration']
        run('test-compile', ['cmake', '--build', b, '--parallel', '2', '--target', *targets])
        run('adaptive-policy', [b/'checkSllAdaptivePolicy'])
        real = json.loads(a.fixture_config.read_text())
        fixture = {k: v for k, v in real.items() if not k.startswith('DSP timing ') and k != 'SRL control metadata file'}
        result['fixture_omitted_design_specific_metadata'] = sorted(set(real)-set(fixture))
        (e/'fixture-base.json').write_text(json.dumps(fixture, indent=2)+'\n')
        run('regression-suite', ['python3', '-B', r/'scripts/diagnostics/test_sll_frozen_qp_audit.py', '--build', b,
                                '--fixture-config', e/'fixture-base.json', '--output', e/'regression-suite'])
        run('adaptive-integration', [b/'checkSllAdaptiveIntegration', e/'regression-suite/fixture.json', e/'adaptive-integration'])
        for mode in ('adaptive', 'baseline'):
            for q in (7, 8, 13):
                out = e/f'{mode}-qp-{q}-replay.json'
                run(f'{mode}-qp-{q}-replay', [b/'replayFrozenSllQp', e/f'adaptive-integration/{mode}/frozen-qp/qp-{q}', out, '24'])
                data = json.loads(out.read_text())
                assert data['coefficient'] == 24 and data['model_gradient_error'] <= 1e-11
                assert [v['tau'] for v in data['variants']] == ([10, .2, .1] if q < 13 else [5, .2, .1])
                for variant in data['variants']:
                    assert variant['relative_residual'] <= 1e-8
                    for i, target in enumerate(variant['targets']):
                        total = sum(net['target_contributions'][i] for net in variant['incident_net_contributions'])
                        assert abs(total-target['all_net_SLL_gradient']) <= 1e-10*(1+abs(total))
                result['masked_replays'].append(dict(mode=mode, qp=q, gradient_error=data['model_gradient_error'],
                                                    max_residual=max(v['relative_residual'] for v in data['variants'])))
        run('timing-summary', ['python3', '-B', r/'scripts/diagnostics/summarize_sll_adaptive.py',
                              e/'adaptive-integration/adaptive', '--output', e/'adaptive-timings.json'])
        timings = json.loads((e/'adaptive-timings.json').read_text())
        for key in ('discrete_count', 'short_span_list_update', 'qp_solve_window', 'original_writeback_window', 'smooth_and_gradient', 'trace_and_log'):
            assert timings['category_totals'][key]['seconds'] > 0
        assert any(int(row['excluded_same_die_nets']) > 0 and int(row['excluded_cross_die_nets']) > 0 for row in timings['per_qp_exclusion'])
        capabilities = json.loads(subprocess.check_output([b/'AMFPlacer', '--capabilities'], text=True))
        assert capabilities['sll_edge_compact_exclusion_schema'] == 1
        result['capabilities'] = capabilities
        result['binaries'] = {name: hashlib.sha256((b/name).read_bytes()).hexdigest() for name in ['AMFPlacer', 'partitionHyperGraph']}
        result['state'] = 'passed'
    except Exception as exc:
        result.update(state='failed', error=str(exc))
        raise
    finally:
        result['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (e/'tests.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
