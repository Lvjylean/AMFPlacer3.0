"""Recorded input inspection or partial hard-resource legalization; no full placement or routing."""
import datetime as dt
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def inspect(root, args, resources=False, initial=False):
    from amf3 import save, stamp, git
    root = Path(root)
    config_path = (root / args.config).resolve()
    config = json.loads(config_path.read_text())
    binary = (root / args.binary).resolve()
    if not binary.is_file():
        raise ValueError('Missing AMF binary: ' + str(binary))
    inputs = {}
    for key in ('vivado extracted device information file', 'vivado extracted design information file',
                'physical boundary model file', 'special pin offset info file', 'clock file', 'mergedSharedCellType2sharedCellType',
                'cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file',
                'resource initial locations file', 'fixed units file', 'DSP registered outputs file',
                'clock resource capacity file', 'external floorplan membership file',
                'external floorplan regions file', 'external floorplan manifest file'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = {'path': str(p), 'sha256': digest(p)}
            config[key] = str(p)
    from paper_boundary_config import validate_paper_boundary_binary
    validate_paper_boundary_binary(config, binary)
    from import_external_floorplan import validate_external_inputs
    validate_external_inputs(config, binary)
    if config.get('clock resource capacity file'):
        from clock_resource_capacity import validate_capacity_inputs, validate_capacity_binary
        validate_capacity_inputs(config['clock resource capacity file'],
                                 config['vivado extracted device information file'],
                                 config.get('clock resource capacity part'))
        validate_capacity_binary(binary)
    if config.get('PhysicalBoundaryMode') == 'true' or config.get('BoundaryAwareClustering') == 'true':
        if not config.get('physical boundary model file'):
            raise ValueError('Physical mode requires an explicit boundary model')
    if config.get('physical boundary model file'):
        from build_physical_boundaries import validate_model_inputs
        validate_model_inputs(config['physical boundary model file'], config['vivado extracted device information file'])
    directory = root / 'experiments/preflight' / (('initial-placement-' if initial else 'resource-legalization-' if resources else 'input-inspection-') + stamp())
    directory.mkdir(parents=True, exist_ok=False)
    if initial:
        config['dumpDirectory'] = str(directory / 'placement')
        (directory / 'placement').mkdir()
        if config.get('external floorplan membership file'):
            config['external floorplan report file'] = str(directory / 'external_floorplan.json')
    save(directory / 'config.json', config)
    command = [str(binary), str(directory / 'config.json')] + (
        ['--inspect-initial-placement', str(directory / 'initial_placement.json')] if initial else
        ['--legalize-resources', str(directory)] if resources else ['--inspect-input', str(directory / 'inputs.json')])
    if (resources or initial) and shutil.which('stdbuf'):
        command = [shutil.which('stdbuf'), '-oL', '-eL'] + command
    manifest = dict(schema='amf-input-run-v1', command=command, source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), binary=str(binary), binary_sha256=digest(binary),
                    inputs=inputs, config_sha256=digest(directory / 'config.json'),
                    placement_executed=resources or initial, initialization_only=initial, full_placement_executed=False, started=dt.datetime.now().astimezone().isoformat())
    build_manifest = binary.parent.parent / 'manifest.json'
    if build_manifest.is_file():
        manifest['build_manifest'] = str(build_manifest)
        manifest['build_manifest_sha256'] = digest(build_manifest)
    save(directory / 'manifest.json', manifest)
    save(directory / 'status.json', {'state': 'running'})
    print(str(directory), flush=True)
    start = time.monotonic()
    with (directory / 'amf.log').open('w') as log:
        result = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
    status = dict(state='completed' if result.returncode == 0 else 'failed', exit_code=result.returncode,
                  elapsed_seconds=time.monotonic() - start, finished=dt.datetime.now().astimezone().isoformat())
    if result.returncode == 0 and not initial and config.get('clock resource capacity file'):
        try:
            report = json.loads((directory / 'inputs.json').read_text()).get('clock_resource_capacity', {})
            if (report.get('mode') != 'device_table' or
                    report.get('source_file') != config['clock resource capacity file'] or
                    report.get('metadata', {}).get('part') != config['clock resource capacity part']):
                raise ValueError('AMF did not confirm the requested clock capacity table')
        except (OSError, ValueError) as error:
            status.update(state='failed', exit_code=1, error=str(error))
    save(directory / 'status.json', status)
    if status['exit_code']:
        raise RuntimeError('AMF input/resource stage failed: ' + str(directory / 'amf.log'))
    print(json.dumps(status))
