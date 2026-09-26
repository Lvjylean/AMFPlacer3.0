"""Recorded input-only run; does not execute placement or Vivado routing."""
import datetime as dt
import hashlib
import json
import subprocess
import time
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def inspect(root, args):
    from amf3 import save, stamp, git
    root = Path(root)
    config_path = (root / args.config).resolve()
    config = json.loads(config_path.read_text())
    binary = (root / args.binary).resolve()
    if not binary.is_file():
        raise ValueError('Missing AMF binary: ' + str(binary))
    inputs = {}
    for key in ('vivado extracted device information file', 'vivado extracted design information file',
                'special pin offset info file', 'clock file', 'mergedSharedCellType2sharedCellType'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = {'path': str(p), 'sha256': digest(p)}
            config[key] = str(p)
    directory = root / 'experiments/preflight' / ('input-inspection-' + stamp())
    directory.mkdir(parents=True, exist_ok=False)
    save(directory / 'config.json', config)
    command = [str(binary), str(directory / 'config.json'), '--inspect-input', str(directory / 'inputs.json')]
    manifest = dict(schema='amf-input-run-v1', command=command, source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), binary=str(binary), binary_sha256=digest(binary),
                    inputs=inputs, config_sha256=digest(directory / 'config.json'),
                    placement_executed=False, started=dt.datetime.now().astimezone().isoformat())
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
        raise RuntimeError('Input inspection failed: ' + str(directory / 'amf.log'))
    print(json.dumps(status))
