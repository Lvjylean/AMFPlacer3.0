#!/usr/bin/env python3
"""Opt-in C++ integration checks against real legacy and U250 device archives."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import zipfile


def archive(path, member, text):
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as z:
        z.writestr(member, text)


def run(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    lut = out / 'lut.zip'
    uram = out / 'uram.zip'
    archive(lut, 'allCellPinNet', 'curCell=> lut type=> LUT1\n')
    archive(uram, 'allCellPinNet', 'curCell=> mem type=> URAM288\n'
            'pin=> mem/CLK refpin=> CLK dir=> IN net=> clk drivepin=> @PORT/clk\n'
            'pin=> mem/EN_A refpin=> EN_A dir=> IN net=> zero drivepin=> <const0>\n')
    (out / 'clocks').write_text('@PORT/clk\n')
    malformed = out / 'malformed.zip'
    with zipfile.ZipFile(args.legacy_device) as z:
        first_line = z.read(z.namelist()[0]).decode().splitlines()[0]
    archive(malformed, 'exportSiteLocation', first_line + ' slr=> -1 prohibited=> 0\n')
    checks = []

    def check(name, device, design, inspect=True, expected=0, message=None, clocks=False):
        config = {'device': name, 'jobs': '1',
                  'vivado extracted device information file': str(device.resolve()),
                  'vivado extracted design information file': str(design)}
        if clocks:
            config['clock file'] = str(out / 'clocks')
        config_path = out / (name + '.json')
        config_path.write_text(json.dumps(config))
        command = [str(args.binary.resolve()), str(config_path)]
        report = out / (name + '-report.json')
        if inspect:
            command += ['--inspect-input', str(report)]
        start = time.monotonic()
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + '.log')).write_text(result.stdout)
        checks.append(dict(name=name, command=command, exit_code=result.returncode,
                           expected_exit_code=expected, elapsed_seconds=time.monotonic() - start))
        (out / 'results.json').write_text(json.dumps(checks, indent=2))
        assert result.returncode == expected, (name, result.returncode, result.stdout[-1500:])
        if message:
            assert message in result.stdout, name
        return json.loads(report.read_text()) if expected == 0 else None

    result = check('legacy', args.legacy_device, lut)
    assert result['cell_count'] == 1 and result['slr_count'] == 1
    assert result['slrs'][0]['id'] == 0
    check('malformed-slr', malformed, lut, expected=2, message='SLR')
    check('uram-capacity', args.legacy_device, uram, expected=2, message='URAM demand')
    check('missing-clock', args.legacy_device, lut, expected=2, message='Configured clock', clocks=True)
    result = check('u250', args.u250_device, uram, clocks=True)
    assert result['cell_types']['URAM288'] == 1 and result['slr_count'] == 4
    assert result['clock_loads'] == {'@PORT/clk': 1}
    check('u250-placement-guard', args.u250_device, lut, inspect=False, expected=2,
          message='Multi-SLR placement is not enabled')
    inputs = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in [args.binary, args.legacy_device, args.u250_device]}
    (out / 'manifest.json').write_text(json.dumps({'inputs_sha256': inputs, 'checks_passed': len(checks)}, indent=2))
    print(json.dumps({'checks_passed': len(checks), 'directory': str(out)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ['binary', 'legacy-device', 'u250-device', 'output']:
        parser.add_argument('--' + key, type=Path, required=True)
    run(parser.parse_args())
