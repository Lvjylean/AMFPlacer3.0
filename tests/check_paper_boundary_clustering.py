#!/usr/bin/env python3
"""Exercise production clustering, anchor assembly and spreading on real U250 geometry."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from check_resource_legalization import archive, cell

def run(root, binary, output):
    output.mkdir(parents=True, exist_ok=False)
    base = json.loads((root / 'configs/experiments/getrf-external-floorplan-paper-boundaries-10ns.json').read_text())
    for key in list(base):
        if key.startswith('external floorplan') or key.startswith('clock resource') or key == 'clock file':
            base.pop(key)
    base.update(jobs='1', MKL='false', BoundaryPaperPathLengthThreshold='1')
    text = cell('source', 'FDRE', [('Q','OUT','n0','source/Q')])
    for i in range(64):
        driver = 'source/Q' if i == 0 else f'v{i-1}/O'
        text += cell(f'v{i}', 'LUT1', [('I0','IN',f'n{i}',driver), ('O','OUT',f'n{i+1}',f'v{i}/O')])
    text += cell('sink', 'FDRE', [('D','IN','n64','v63/O')])
    results = []
    manifest = dict(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    state='running', scenarios=results)
    for scenario in ('joint', 'right', 'upper', 'both-seams', 'side-tie', 'slr-tie', 'inside', 'macro', 'negative-offset', 'oversized', 'bottom', 'top', 'bottom-macro', 'top-macro', 'qp'):
        d = output / scenario; d.mkdir()
        archive(d/'netlist.zip', 'allCellPinNet', text)
        cfg = dict(base, **{'vivado extracted design information file':str(d/'netlist.zip'),
                           'dumpDirectory':str(d), 'BoundaryReportDirectory':str(d)})
        (d/'config.json').write_text(json.dumps(cfg, indent=2)+'\n')
        started = time.monotonic()
        with (d/'run.log').open('w') as f:
            result = subprocess.run([str(binary), str(d/'config.json'), scenario, str(d/'result.tsv')],
                                    stdout=f, stderr=subprocess.STDOUT, timeout=240,
                                    env=dict(os.environ, OMP_NUM_THREADS='1'))
        results.append(dict(scenario=scenario, exit_code=result.returncode, elapsed_seconds=time.monotonic()-started))
        if result.returncode:
            manifest['state']='failed'; (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            raise RuntimeError((d/'run.log').read_text()[-5000:])
        if scenario not in ('slr-tie', 'oversized'):
            rows = list(csv.DictReader((d/'qp_region_targets.tsv').open(), delimiter='\t'))
            assert len(rows) == (7 if scenario == 'qp' else 5)
            assert rows[0]['stage'] == 'slr' and int(rows[0]['x_attractions']) == 0
            assert (int(rows[0]['y_attractions']) == 0) == (scenario == 'inside')
            assert int(rows[1]['preferences']) > 0 and int(rows[1]['x_attractions']) == int(rows[1]['y_attractions']) == 0
            for row in rows:
                assert not (int(row['x_attractions']) and int(row['y_attractions'])), 'Simultaneous XY attraction'
                if row['stage'] == 'hpio': assert int(row['y_attractions']) == 0
            assert rows[-1]['stage'] == 'hpio' and int(rows[-1]['x_attractions']) == 0, 'Reversed pull after center'
            hpio = [row for row in rows[:-1] if row['stage'] == 'hpio']
            assert hpio and ((int(hpio[0]['x_attractions']) == 0) == (scenario == 'side-tie'))
            spreads = list(csv.DictReader((d/'paper_spreading.tsv').open(), delimiter='\t'))
            assert all(row['axis'] == ('X' if row['stage'] == 'slr' else 'Y') for row in spreads)
    manifest['state']='completed'; (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest))

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    for name in ('root','binary','output'): parser.add_argument('--'+name, type=Path, required=True)
    args=parser.parse_args(); run(args.root.resolve(), args.binary.resolve(), args.output.resolve())
