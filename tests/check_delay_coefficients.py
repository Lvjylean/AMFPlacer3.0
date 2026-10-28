#!/usr/bin/env python3
"""Check the production delay interfaces/STA on U250 and legacy VCU108/095."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import time
import zipfile
from check_resource_legalization import archive, cell


def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def run(args):
    root, binary, out = args.root.resolve(), args.binary.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    config = json.loads((root / 'configs/experiments/getrf-u250-tile-columns.json').read_text())
    profile_path = root / 'configs/calibration/u250-tile-columns-delay-20260929.json'
    profile = json.loads(profile_path.read_text())
    model_path = Path(config['physical boundary model file'])
    assert hashlib.sha256(model_path.read_bytes()).hexdigest() == profile['physical_model_sha256']
    sites, boundaries = {}, []
    for line in model_path.read_text().splitlines():
        fields = line.split('\t')
        if fields[0] == 'SITE':
            sites[fields[1]] = tuple(map(float, fields[3:5]))
        elif fields[0] == 'BOUNDARY':
            boundaries.append(fields)
    coefficients = profile['recommended_local_candidate_coefficients_ps']

    def expected(coords, slr_penalty):
        x0, y0, x1, y1 = coords
        dx, dy = f32(abs(x0-x1)), f32(abs(y0-y1))
        radius2 = f32(f32(dx*dx) + f32(dy*dy))
        band = 'near' if radius2 < 9 else 'middle' if radius2 < 36 else 'far'
        c = list(map(f32, coefficients[band]))
        delay = max(.05, (c[0] + c[1]*(2*dx)**.3 + c[2]*dy**.3 +
                          c[3]*(2*dx)**.5 + c[4]*dy**.5)/1000)
        for b in boundaries:
            if b[8] != '1':
                continue
            position, low, high, penalty = map(float, b[4:8])
            a, z, p, q = (x0, x1, y0, y1) if b[3] == 'X' else (y0, y1, x0, x1)
            crossed = ((a <= position) != (z <= position)) if b[2] == 'SLR' else (
                ((a < position < z) or (z < position < a)) and low <= p <= high and low <= q <= high)
            if crossed:
                delay += slr_penalty if b[2] == 'SLR' else penalty
        return delay, band

    design = (cell('source', 'FDRE', [('Q', 'OUT', 'a', 'source/Q')]) +
              cell('mid', 'LUT1', [('I0', 'IN', 'a', 'source/Q'), ('O', 'OUT', 'b', 'mid/O')]) +
              cell('sink', 'FDRE', [('D', 'IN', 'b', 'mid/O')]))
    archive(out/'netlist.zip', 'allCellPinNet', design)
    config.pop('clock file', None)
    config.update({'jobs': '1', 'ClockPeriod': '10',
                   'vivado extracted design information file': str(out/'netlist.zip')})
    pairs = []
    sample_files = []
    for layout in (0, 1):
        sample = root / f'experiments/preflight/20260929-u250-tile-columns-delay-01/layout-{layout}/samples.tsv'
        sample_files.append(sample)
        for i, row in enumerate(csv.DictReader(sample.open(), delimiter='\t')):
            pairs.append((f'sample-{layout}-{i}', *sites[row['source_site']], *sites[row['sink_site']]))
    for radius in (0, 2.9999, 3, 3.0001, 5.9999, 6, 6.0001):
        for i in range(9):
            angle = i*math.pi/16
            pairs.append((f'threshold-{radius}-{i}', 1, 20, 1+radius*math.cos(angle), 20+radius*math.sin(angle)))
    rng = random.Random(20260929)
    for i in range(256):
        pairs.append((f'full-device-{i}', rng.uniform(.25,150.75), rng.uniform(0,959),
                      rng.uniform(.25,150.75), rng.uniform(0,959)))
    # Ordinary CR, exact IO endpoints, SLR seams, three-seam span and SLR3.
    special = [(20,59,20,60),(74,20,75,20),(74,20,74.75,20),(74.75,20,75,20),
               (20,239,20,239.5),(20,239.5,20,240),(20,479,20,480),
               (20,719,20,720),(74,239,75,240),(1,0,150,959),(20,750,50,800)]
    pairs += [(f'boundary-{i}', *p) for i,p in enumerate(special)]
    pairs += [('sta-near',20,20,20,21), ('sta-middle',20,20,20,24),
              ('sta-far',20,20,20,40), ('sta-cross',74,230,80,250)]
    checks = []

    def check(name, cfg, data, is_u250=True):
        directory = out/name
        directory.mkdir()
        cfg = dict(cfg, dumpDirectory=str(directory))
        data = [(name, *map(f32, xyz)) for name,*xyz in data]
        (directory/'config.json').write_text(json.dumps(cfg, indent=2)+'\n')
        (directory/'pairs.tsv').write_text(''.join(' '.join(map(str,p))+'\n' for p in data))
        command = [str(binary),str(directory/'config.json'),str(directory/'pairs.tsv'),str(directory/'result.tsv')]
        start = time.monotonic()
        with (directory/'run.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180,
                                    env={**os.environ, 'OMP_NUM_THREADS':'1'})
        assert result.returncode == 0, (name, result.returncode, (directory/'run.log').read_text()[-3000:])
        rows = list(csv.DictReader((directory/'result.tsv').open(), delimiter='\t'))
        assert len(rows) == len(data)
        max_error, bands, sta, changed = 0, dict(near=0,middle=0,far=0), [], 0
        for row, (label,*xyz) in zip(rows,data):
            assert row['name'] == label
            actual, legacy = float(row['actual_ns']), float(row['legacy_ns'])
            assert actual == float(row['node_ns']) == float(row['reverse_ns'])
            if is_u250:
                prediction, band = expected(xyz,float(cfg['SLRBoundaryDelayNs']))
                error = abs(actual-prediction)
                assert error < 3e-6, (name,label,actual,prediction)
                max_error = max(max_error,error)
                bands[band] += 1
                if band == 'near': assert actual == legacy, (name,label)
                elif actual != legacy: changed += 1
            else:
                assert actual == legacy, (name,label)
            if label.startswith('sta-'): sta.append(row)
        assert len(sta) == 4
        for row in sta[1:]:
            delta = 2*(float(row['actual_ns'])-float(sta[0]['actual_ns']))
            assert abs(float(row['sta_ns'])-float(sta[0]['sta_ns'])-delta) < 1e-5, (name,row,'STA')
            assert abs(float(row['slack_ns'])-float(sta[0]['slack_ns'])+delta) < 1e-5, (name,row,'slack')
        if is_u250: assert changed > 0
        checks.append(dict(name=name, rows=len(rows), bands=bands, changed_from_legacy=changed,
                           max_abs_error_ns=max_error, sta_cases=len(sta),
                           elapsed_seconds=time.monotonic()-start, exit_code=result.returncode, command=command))
        (out/'results.json').write_text(json.dumps(checks,indent=2)+'\n')

    check('u250-new-coordinates', config, pairs)
    check('u250-slr-penalty-zero', dict(config, SLRBoundaryDelayNs='0'), pairs[-15:])
    legacy = dict(config, device='VCU108', PhysicalBoundaryMode='false', PhysicalBoundaryAudit='false')
    legacy.pop('physical boundary model file', None)
    legacy.pop('physical device part', None)
    legacy_device = root/'benchmarks/VCU108/device/exportSiteLocation.zip'
    legacy['vivado extracted device information file'] = str(legacy_device)
    for key in ('cellType2fixedAmo file','cellType2sharedCellType file','sharedCellType2BELtype file',
                'mergedSharedCellType2sharedCellType'):
        legacy[key] = str(root/'benchmarks/VCU108/compatibleTable'/Path(config[key]).name)
    with zipfile.ZipFile(legacy_device) as z:
        coords = [tuple(map(float,m)) for m in re.findall(r'centerx=> (\S+) centery=> (\S+)',z.read(z.namelist()[0]).decode())]
    legacy_pairs = [(f'legacy-{i}',*rng.choice(coords),*rng.choice(coords)) for i in range(256)]
    legacy_pairs += [p for p in pairs if p[0].startswith('threshold-')] + pairs[-4:]
    check('vcu108-095-bitwise-legacy', legacy, legacy_pairs, is_u250=False)
    manifest = dict(state='completed', checks=checks, no_placement_or_routing=True,
                    binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    input_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in [profile_path,model_path,legacy_device,
                                            Path(config['vivado extracted device information file']),*sample_files]})
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root','binary','output'): parser.add_argument('--'+key,type=Path,required=True)
    run(parser.parse_args())
