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


def inspect(root, args, resources=False):
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
                'resource initial locations file', 'fixed units file', 'DSP registered outputs file'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = {'path': str(p), 'sha256': digest(p)}
            config[key] = str(p)
    if config.get('PhysicalBoundaryMode') == 'true' or config.get('BoundaryAwareClustering') == 'true':
        if not config.get('physical boundary model file'):
            raise ValueError('Physical mode requires an explicit boundary model')
    if config.get('physical boundary model file'):
        from build_physical_boundaries import validate_model_inputs
        validate_model_inputs(config['physical boundary model file'], config['vivado extracted device information file'])
    directory = root / 'experiments/preflight' / (('resource-legalization-' if resources else 'input-inspection-') + stamp())
    directory.mkdir(parents=True, exist_ok=False)
    save(directory / 'config.json', config)
    command = [str(binary), str(directory / 'config.json')] + (
        ['--legalize-resources', str(directory)] if resources else ['--inspect-input', str(directory / 'inputs.json')])
    if resources and shutil.which('stdbuf'):
        command = [shutil.which('stdbuf'), '-oL', '-eL'] + command
    manifest = dict(schema='amf-input-run-v1', command=command, source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), binary=str(binary), binary_sha256=digest(binary),
                    inputs=inputs, config_sha256=digest(directory / 'config.json'),
                    placement_executed=resources, full_placement_executed=False, started=dt.datetime.now().astimezone().isoformat())
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
    save(directory / 'status.json', status)
    if result.returncode:
        raise RuntimeError('AMF input/resource stage failed: ' + str(directory / 'amf.log'))
    print(json.dumps(status))
