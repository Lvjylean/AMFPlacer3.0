#!/usr/bin/env python3
"""Exercise hard-resource allocation and cascade seams through the real C++ placer."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile


def archive(path, member, text):
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as z:
        z.writestr(member, text)


def cell(name, kind, pins=()):
    return 'curCell=> %s type=> %s\n' % (name, kind) + ''.join(
        'pin=> %s/%s refpin=> %s dir=> %s net=> %s drivepin=> %s\n' %
        (name, pin, pin, direction, net, driver)
        for pin, direction, net, driver in pins)


def chain(kind, output, input_):
    return cell('a', kind, [(output, 'OUT', 'cascade', 'a/' + output)]) + cell(
        'b', kind, [(input_, 'IN', 'cascade', 'a/' + output)])


def run(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    root = args.root.resolve()
    device = root / 'data/devices/u250-vivado-2024.2/exportSiteLocation.zip'
    base = json.loads((root / 'configs/experiments/getrf-u250-resources.json').read_text())
    base.pop('clock file', None)
    base['jobs'] = '1'
    for key in list(base):
        if key.endswith(' file') or key == 'mergedSharedCellType2sharedCellType':
            base[key] = str(root / base[key])
    checks = []

    def check(name, design, device_=device, seeds='', message=None, fixed='', overrides=None):
        directory = out / name
        directory.mkdir()
        archive(directory / 'netlist.zip', 'allCellPinNet', design)
        cfg = dict(base)
        cfg.update(overrides or {})
        cfg['vivado extracted device information file'] = str(device_)
        cfg['vivado extracted design information file'] = str(directory / 'netlist.zip')
        if fixed:
            (directory / 'fixed').write_text('# fixed hard resources\n' + fixed)
            cfg['fixed units file'] = str(directory / 'fixed')
        if seeds:
            (directory / 'seeds').write_text(seeds)
            cfg['resource initial locations file'] = str(directory / 'seeds')
        (directory / 'config.json').write_text(json.dumps(cfg, indent=2))
        command = [str(args.binary.resolve()), str(directory / 'config.json'), '--legalize-resources', str(directory)]
        start = time.monotonic()
        with (directory / 'run.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        text = (directory / 'run.log').read_text()
        expected = 2 if message else 0
        checks.append(dict(name=name, exit_code=result.returncode, expected=expected,
                           elapsed_seconds=time.monotonic() - start, command=command))
        (out / 'results.json').write_text(json.dumps(checks, indent=2))
        assert result.returncode == expected, (name, result.returncode, text[-2500:])
        if message:
            assert message in text, (name, text[-2500:])
            return None
        rows = list(csv.DictReader((directory / 'resources.tsv').open(), delimiter='\t'))
        assert len({(r['site'], r['bel']) for r in rows}) == len(rows), name
        return {r['cell']: r for r in rows}

    def adjacent(rows):
        a, b = rows['a'], rows['b']
        assert a['slr'] == b['slr'], rows
        ax, ay = map(int, re.search(r'_X(\d+)Y(\d+)', a['site']).groups())
        bx, by = map(int, re.search(r'_X(\d+)Y(\d+)', b['site']).groups())
        assert ax == bx and by == ay + 1, rows

    independent = cell('a', 'URAM288', [('DOUT_A[0]', 'OUT', 'data', 'a/DOUT_A[0]')]) + cell(
        'b', 'URAM288_BASE', [('DIN_A[0]', 'IN', 'data', 'a/DOUT_A[0]')])
    rows = check('uram-cross-slr-data', independent, seeds='a 56 10\nb 56 730\n')
    assert rows['a']['slr'] != rows['b']['slr'], rows
    assert all(r['bel'] == 'URAM_288K_INST' for r in rows.values())
    rows = check('uram-locked', independent, seeds='b 56 0\n',
                 fixed='name=> a loc=> URAM288_X0Y0 bel=> URAM_288K_INST\n')
    assert rows['a']['site'] == 'URAM288_X0Y0' and rows['b']['site'] != rows['a']['site']
    rows = check('uram-competition', ''.join(cell('u%d' % i, 'URAM288') for i in range(8)))
    assert len(rows) == 8
    carry = chain('CARRY8', 'CO[7]', 'CI')
    dsp = chain('DSP48E2', 'PCOUT[0]', 'PCIN[0]')
    for output in ['O[5]', 'CO[3]']:
        rows = check('carry-fabric-' + output[:2], chain('CARRY8', output, 'CI'), seeds='a 0 10\nb 0 730\n')
        assert rows['a']['slr'] != rows['b']['slr'], rows
    adjacent(check('carry-seam', carry, seeds='a 0 239\n'))
    adjacent(check('dsp-seam', dsp, seeds='a 3 237.5\n'))
    adjacent(check('dsp-multsign', chain('DSP48E2', 'MULTSIGNOUT', 'MULTSIGNIN'), seeds='a 3 237.5\n'))
    check('uram-cascade-rejected', chain('URAM288', 'CAS_OUT_DOUT_A[0]', 'CAS_IN_DIN_A[0]'),
          message='URAM cascade is not supported')

    with zipfile.ZipFile(device) as z:
        lines = z.read(z.namelist()[0]).decode().splitlines(True)

    def restricted(name, prefix, allowed):
        path = out / (name + '.zip')
        archive(path, 'exportSiteLocation', ''.join(
            line.replace('prohibited=> 0', 'prohibited=> 1')
            if line.startswith('site=> ' + prefix) and line.split()[1] not in allowed else line
            for line in lines))
        return path

    fractional = restricted('carry-fractional-budget', 'SLICE_',
        {'SLICE_X%dY%d' % (x, y) for x in (0, 1) for y in range(11)})
    rows = check('carry-fractional-budget', ''.join(cell('c%d' % i, 'CARRY8') for i in range(10)),
        fractional, seeds=''.join('c%d 0 %d\n' % (i, i) for i in range(10)))
    assert len(rows) == 10 and any(r['site'].startswith('SLICE_X1') for r in rows.values()), rows

    seam_carry = restricted('carry-only-seam', 'SLICE_', {'SLICE_X0Y239', 'SLICE_X0Y240'})
    check('carry-impossible-seam', carry, seam_carry, message='No contiguous same-SLR resource range')
    seam_dsp = restricted('dsp-only-seam', 'DSP48E2_', {'DSP48E2_X0Y95', 'DSP48E2_X0Y96'})
    check('dsp-impossible-seam', dsp, seam_dsp, message='No contiguous same-SLR resource range')
    one_uram = restricted('one-uram', 'URAM288_', {'URAM288_X0Y0'})
    rows = check('uram-prohibited-sites', cell('a', 'URAM288'), one_uram)
    assert rows['a']['site'] == 'URAM288_X0Y0', rows
    check('uram-capacity-overflow', independent, one_uram, message='URAM demand')
    if args.legacy_device:
        legacy_cfg = {}
        for key in ['cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file']:
            source = Path(base[key])
            target = out / ('legacy-' + source.name)
            target.write_text(''.join(line for line in source.read_text().splitlines(True) if 'URAM' not in line))
            legacy_cfg[key] = str(target)
        rows = check('legacy-dsp', dsp, args.legacy_device.resolve(), overrides=legacy_cfg)
        adjacent(rows)
        rows = check('legacy-carry', carry, args.legacy_device.resolve(), overrides=legacy_cfg)
        adjacent(rows)
    (out / 'manifest.json').write_text(json.dumps({
        'checks_passed': len(checks), 'inputs_sha256': {
            str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.binary, device]}}, indent=2))
    print(json.dumps({'checks_passed': len(checks), 'directory': str(out)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['root', 'binary', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--legacy-device', type=Path)
    run(parser.parse_args())
