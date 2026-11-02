"""Exercise production input, initial packing and both timing graph views."""
import argparse
import json
from pathlib import Path
import subprocess
import time
from check_resource_legalization import cell, archive

p = argparse.ArgumentParser()
for key in ('config', 'checker', 'output'):
    p.add_argument('--' + key, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
cfg = json.loads(a.config.read_text())
for key in list(cfg):
    if key.startswith('ClockPeriod:') or key in (
        'fixed units file', 'fixed occupied sites file', 'clock file',
        'DSP registered outputs file', 'ASYNC_REG groups file',
        'ASYNC_REG proximity pairs file', 'SRL control metadata file',
        'FF control polarities file'):
        cfg.pop(key)
design = ''
for name, driver in [('launch_d', None), ('launch_s', None), ('sink', 'gate1'), ('fan', 'gate0')]:
    pins = [('Q', 'OUT', name, name + '/Q')]
    if driver:
        pins.append(('D', 'IN', driver, driver + '/O'))
    design += cell(name, 'FDRE', pins)
for name, driver in [('gate0', 'launch_d/Q'), ('gate1', 'gate0/O')]:
    design += cell(name, 'AND2B1L', [('DI', 'IN', driver.split('/')[0], driver),
                                  ('SRI', 'IN', 'launch_s', 'launch_s/Q'),
                                  ('O', 'OUT', name, name + '/O')])
archive(a.output / 'netlist.zip', 'allCellPinNet', design)
metadata = 'AMF_FF_CONTROL_POLARITIES 1\n'
metadata += ''.join(f'{n} FDRE 0 0 0\n' for n in ('launch_d', 'launch_s', 'sink', 'fan'))
metadata += 'gate0 AND2B1L 0 0 0\ngate1 AND2B1L 0 1 0\n'
(a.output / 'controls.txt').write_text(metadata)
cfg.update({'allow floating placement': 'true', 'ClockPeriod': '3.332', 'jobs': '4',
            'vivado extracted design information file': str(a.output / 'netlist.zip'),
            'FF control polarities file': str(a.output / 'controls.txt'),
            'dumpDirectory': str(a.output)})
results = []
for boundary in ('true', 'false'):
    cfg['CompositeIOTimingBoundary'] = boundary
    config = a.output / (boundary + '.json')
    config.write_text(json.dumps(cfg, indent=2) + '\n')
    command = [str(a.checker), str(config), 'fixture', str(a.output / (boundary + '.tsv'))]
    start = time.monotonic()
    with (a.output / (boundary + '.log')).open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    results.append({'boundary_mode': boundary, 'command': command, 'exit_code': result.returncode,
                    'wall_seconds': time.monotonic() - start})
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError((a.output / (boundary + '.log')).read_text()[-4000:])
print('PASS AND2B1L production graph and physical control regression in both I/O modes')
