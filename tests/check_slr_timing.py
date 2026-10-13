#!/usr/bin/env python3
"""Probe production C++ timing with real U250/VCU108 coordinates and a small FF-LUT-FF path."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile
from check_resource_legalization import archive, cell


def run(args):
    root, binary, out = args.root.resolve(), args.binary.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    base = json.loads((root / 'configs/experiments/getrf-u250-full.json').read_text())
    base.pop('clock file', None)
    base.pop('SLRBoundaryDelayNs', None)
    base['jobs'] = '1'
    base['ClockPeriod'] = '1'  # Toy-path constraint, not the GETRF experiment target.
    for key in list(base):
        if key.endswith(' file') or key == 'mergedSharedCellType2sharedCellType':
            base[key] = str(root / base[key])
    design = (cell('source', 'FDRE', [('Q', 'OUT', 'a', 'source/Q')]) +
              cell('mid', 'LUT1', [('I0', 'IN', 'a', 'source/Q'), ('O', 'OUT', 'b', 'mid/O')]) +
              cell('sink', 'FDRE', [('D', 'IN', 'b', 'mid/O')]))
    archive(out / 'netlist.zip', 'allCellPinNet', design)
    base['vivado extracted design information file'] = str(out / 'netlist.zip')
    device = Path(base['vivado extracted device information file'])
    with zipfile.ZipFile(device) as z:
        raw = z.read(z.namelist()[0]).decode()

    def coords(text):
        return {m[1]: (float(m[2]), float(m[3])) for m in re.finditer(
            r'site=> (\S+).*?centerx=> (\S+) centery=> (\S+)', text)}

    sites = coords(raw)
    pairs = []
    def pair(name, a, b, crossings):
        pairs.append((name, *sites[a], *sites[b], crossings))
    for seam in (240, 480, 720):
        for gap, start in [(1, seam-1), (4, seam-2), (6, seam-3)]:
            pair(f'seam-{seam}-gap-{gap}', f'SLICE_X117Y{start}', f'SLICE_X117Y{start+gap}', 1)
    for gap in (0, 1, 4, 10):
        pair(f'same-slr-gap-{gap}', 'SLICE_X117Y200', f'SLICE_X117Y{200+gap}', 0)
    pair('ordinary-clock-region', 'SLICE_X117Y59', 'SLICE_X117Y60', 0)
    pair('two-seams', 'SLICE_X117Y239', 'SLICE_X117Y480', 2)
    pair('three-seams', 'SLICE_X117Y239', 'SLICE_X117Y720', 3)
    pair('xy-crossing', 'SLICE_X17Y239', 'SLICE_X117Y240', 1)
    # Verify the exact midpoint follows the existing clock-region lookup convention.
    x, lower = sites['SLICE_X117Y239']; _, upper = sites['SLICE_X117Y240']; mid = (lower+upper)/2
    pairs.extend([('seam-exact-midpoint', x, lower, x, mid, 0),
                  ('seam-above-midpoint', x, lower, x, mid+0.001, 1)])
    pair('reported-critical-net', 'SLICE_X117Y237', 'SLICE_X117Y243', 1)
    checks = []

    def check(name, cfg, data, penalty=1.5, message=None):
        directory = out / name
        directory.mkdir()
        cfg = dict(cfg, dumpDirectory=str(directory))
        (directory/'config.json').write_text(json.dumps(cfg, indent=2))
        (directory/'pairs.tsv').write_text(''.join(' '.join(map(str, p))+'\n' for p in data))
        command = [str(binary), str(directory/'config.json'), str(directory/'pairs.tsv'), str(directory/'result.tsv')]
        start = time.monotonic()
        with (directory/'run.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120,
                                    env={**__import__('os').environ, 'OMP_NUM_THREADS': '1'})
        log = (directory/'run.log').read_text()
        expected = 2 if message else 0
        assert result.returncode == expected, (name, result.returncode, log[-3000:])
        count = 0
        if message:
            assert message in log, (name, log[-2000:])
        else:
            rows = list(csv.DictReader((directory/'result.tsv').open(), delimiter='\t'))
            assert len(rows) == len(data)+2
            for row in rows:
                old, new = float(row['old_ns']), float(row['new_ns'])
                expected_delta = int(row['boundaries'])*penalty
                if row['name'] == 'slack_roundtrip': expected_delta *= -1
                assert abs(new-old-expected_delta) < 2e-5, (name, row, expected_delta)
                if row['name'] not in ('sta_roundtrip', 'slack_roundtrip'):
                    assert abs(new-float(row['reverse_ns'])) < 2e-5, (name, row)
                count += 1
        checks.append(dict(name=name, command=command, exit_code=result.returncode,
                           assertions_rows=count, elapsed_seconds=time.monotonic()-start))
        (out/'results.json').write_text(json.dumps(checks, indent=2))

    check('u250-default', base, pairs)
    check('u250-explicit', dict(base, SLRBoundaryDelayNs='1.5'), pairs)
    check('u250-disabled', dict(base, SLRBoundaryDelayNs='0'), pairs, penalty=0)
    physical=dict(base, PhysicalBoundaryMode='true', PhysicalBoundaryAudit='true', SLRBoundaryDelayNs='1.5',
                  **{'physical boundary model file':str(root/'data/devices/u250-physical-v1/model/physical_structure.tsv'),
                     'physical device part':'xcu250-figd2104-2L-e'})
    check('u250-physical-unified',physical,pairs)
    check('u250-physical-slr-disabled',dict(physical,SLRBoundaryDelayNs='0'),pairs,penalty=0)
    check('u250-configured', dict(base, SLRBoundaryDelayNs='2.5'), pairs, penalty=2.5)
    # IDs need not be ordered or consecutive: count actual seams from geometry.
    remapped = re.sub(r'slr=> (\d+)', lambda m: 'slr=> '+str([30, 20, 70, 10][int(m[1])]), raw)
    archive(out/'remapped.zip', 'exportSiteLocation', remapped)
    check('u250-remapped-ids', dict(base, **{'vivado extracted device information file': str(out/'remapped.zip')}), pairs)
    malformed = re.sub(r'slr=> \d+', 'slr=> 99', raw, count=1)
    archive(out/'malformed.zip', 'exportSiteLocation', malformed)
    check('mixed-slr-row-rejected', dict(base, **{'vivado extracted device information file': str(out/'malformed.zip')}), pairs,
          message='horizontally aligned SLR rows')
    legacy = root/'benchmarks/VCU108/device/exportSiteLocation.zip'
    with zipfile.ZipFile(legacy) as z: legacy_sites = coords(z.read(z.namelist()[0]).decode())
    entries = sorted((n, xy) for n, xy in legacy_sites.items() if n.startswith('SLICE_'))
    legacy_pairs = [('legacy-span', *entries[0][1], *entries[-1][1], 0)]
    legacy_cfg = dict(base, device='VCU108', **{'vivado extracted device information file': str(legacy)})
    for key in ('cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file', 'mergedSharedCellType2sharedCellType'):
        legacy_cfg[key] = str(root/'benchmarks/VCU108/compatibleTable'/Path(base[key]).name)
    check('legacy-default', legacy_cfg, legacy_pairs)
    for i, value in enumerate(('-1', 'nan', 'inf', '1.5ns', 'bad', '1e999')):
        check(f'invalid-penalty-{i}', dict(base, SLRBoundaryDelayNs=value), pairs, message='SLRBoundaryDelayNs must be')
    manifest = dict(checks_passed=len(checks), model_rows_checked=sum(c['assertions_rows'] for c in checks),
                    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
                    git_status=subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True),
                    probe_binary=str(binary), probe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    input_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (device, legacy, out/'netlist.zip')},
                    scope='Native timing model and STA regression; no full GETRF placement/routing rerun')
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'binary', 'output'): parser.add_argument('--'+key, type=Path, required=True)
    run(parser.parse_args())
