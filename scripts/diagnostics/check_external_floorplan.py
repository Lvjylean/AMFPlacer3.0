#!/usr/bin/env python3
"""Exercise native soft initialization and unchanged legacy SA using tiny netlists."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import zipfile


def run(template, binary, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    cfg = json.loads(Path(template).read_text())
    for key in ('clock file', 'designCluster', 'fixed units file', 'unpredictable macro file',
                'clock resource capacity file', 'clock resource capacity part'):
        cfg.pop(key, None)
    cfg.update({'jobs': '1', 'dumpDirectory': str(output), 'ClockRegionFabricGeometry': 'true'})
    kinds = dict(pair_a='FDRE', pair_b='FDRE', fixed='FDRE', dsp='DSP48E2',
                 bram='RAMB36E2', uram='URAM288', mem='SRL16E', lut='LUT6')
    kinds.update({f'free{i}': 'FDRE' for i in range(12)})
    cells = []
    for name, kind in kinds.items():
        cells.append(f'curCell=> {name} type=> {kind}\n')
        port = 'Q' if kind in ('FDRE', 'SRL16E') else 'O' if kind == 'LUT6' else 'P' if kind == 'DSP48E2' else 'DOUT_A' if kind == 'URAM288' else 'DOADO[0]'
        cells.append(f'   pin=> {name}/{port} refpin=> {port} dir=> OUT net=> n_{name} drivepin=> {name}/{port}\n')
        if kind == 'FDRE':
            cells.append(f'   pin=> {name}/D refpin=> D dir=> IN net=> const0 drivepin=> <const0>\n')
    with zipfile.ZipFile(output / 'netlist.zip', 'w') as z:
        z.writestr('allCellPinNet', ''.join(cells))
    cfg['vivado extracted design information file'] = str(output / 'netlist.zip')
    regions = 'module\tclock_regions\nA\tX0Y0 X2Y0\nB\tX7Y15\n'
    members = 'cell\tmodule\tref\n' + ''.join(
        f'{name}\t{"B" if name == "pair_b" else "A"}\t{kind}\n' for name, kind in kinds.items())
    (output / 'regions.tsv').write_text(regions)
    (output / 'membership.tsv').write_text(members)
    cfg.update({'external floorplan membership file': str(output / 'membership.tsv'),
                'external floorplan regions file': str(output / 'regions.tsv'),
                'ExternalFloorplanReferenceClusterCount': '54',
                'Simulated Annealing IterNum': 'must-not-be-read',
                'external floorplan report file': str(output / 'external.json')})
    results = []
    expected_positions = None
    for label in ('valid', 'repeat', 'duplicate', 'missing', 'type', 'invalid-cr', 'partial', 'random-conflict', 'legacy'):
        c = dict(cfg)
        (output / 'regions.tsv').write_text(regions)
        (output / 'membership.tsv').write_text(members)
        scenario = 'valid'
        if label == 'duplicate':
            (output / 'membership.tsv').write_text(members + 'free0\tA\tFDRE\n')
        elif label == 'missing':
            (output / 'membership.tsv').write_text(members.replace('free0\tA\tFDRE\n', ''))
        elif label == 'type':
            (output / 'membership.tsv').write_text(members.replace('free0\tA\tFDRE', 'free0\tA\tLUT6'))
        elif label == 'invalid-cr':
            (output / 'regions.tsv').write_text(regions.replace('X2Y0', 'X999Y0'))
        elif label == 'partial':
            c.pop('external floorplan regions file')
        elif label == 'random-conflict':
            c['RandomInitialPlacement'] = 'true'
        elif label == 'legacy':
            c = {k: v for k, v in c.items() if not k.startswith('external floorplan')}
            c.update({'Simulated Annealing IterNum': '10', 'Simulated Annealing restartNum': '1'})
            scenario = 'legacy'
        if label not in ('valid', 'repeat', 'legacy'):
            scenario = 'reject'
        config = output / (label + '.json')
        config.write_text(json.dumps(c, indent=2))
        command = [str(Path(binary).resolve()), str(config), scenario, str(output / (label + '-result.json'))]
        start = time.monotonic()
        with (output / (label + '.log')).open('w') as log:
            r = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        results.append({'case': label, 'command': command, 'exit_code': r.returncode, 'elapsed_seconds': time.monotonic() - start})
        (output / 'results.json').write_text(json.dumps(results, indent=2))
        if r.returncode:
            raise RuntimeError('Native test failed: ' + label)
        text = (output / (label + '.log')).read_text(errors='replace')
        if label == 'legacy':
            if 'SA-based Cluster Placement Start' not in text or 'External floorplan initialization:' in text:
                raise RuntimeError('Legacy dispatch changed')
        elif label in ('valid', 'repeat'):
            report = json.loads((output / 'external.json').read_text())
            assert report['mixed_membership_pus'] == 1 and report['resource_fallback_pus'] >= 1
            assert report['fixed_or_locked_pus'] == 1 and report['legacy_cluster_count'] == 54
            assert not report['persistent_region_constraints'] and not report['sa_executed']
            coords = (output / 'external.json.positions.tsv').read_bytes()
            if expected_positions is not None:
                assert coords == expected_positions, 'Non-deterministic initial coordinates'
            expected_positions = coords
            assert 'SA-based Cluster Placement Start' not in text
    print(json.dumps({'passed': len(results), 'output': str(output)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--template', required=True)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    run(args.template, args.binary, args.output)

