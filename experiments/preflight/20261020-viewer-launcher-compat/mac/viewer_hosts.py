"""Route recorded layouts to their actual host, without changing SSH credentials."""
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent
REMOTE_ROOT = '/Projects/jinyang/workspace/AMFplacer3.0'
SUPPORT = REMOTE_ROOT + '/experiments/preflight/20261026-viewer-072-support'


def host_for_run(run):
    path = PurePosixPath(run)
    if not path.is_absolute() or '..' in path.parts:
        raise ValueError('Invalid absolute run path')
    for root, host in [(REMOTE_ROOT, 'eda072'),
                       ('/Orion/jinyang/workspace/AMFplacer3', 'coldsun')]:
        if PurePosixPath(root) in path.parents:
            return host
    raise ValueError('Run does not belong to a configured AMF host')


def remote_072(run=None):
    import shlex
    command = ['python3', '-B', SUPPORT + '/remote.py']
    if run:
        if host_for_run(run) != 'eda072':
            raise ValueError('Wrong host for run')
        command += ['--run', run]
    result = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                             'eda072', shlex.join(command)],
                            capture_output=True, text=True, timeout=150)
    if result.returncode:
        raise RuntimeError('072 查看器连接失败：' + result.stderr[-1000:].strip())
    return json.loads(result.stdout)


def combined_catalog(remote_099):
    """One unavailable host must not hide the other host or erase its old list."""
    try:
        cached = json.loads((ROOT/'amf-picker-data.json').read_text())['runs']
    except (OSError, ValueError, KeyError):
        cached = []
    records, failures = [], []
    with ThreadPoolExecutor(max_workers=2) as executor:
        tasks = {host: executor.submit(fn) for host, fn in
                 [('coldsun', remote_099), ('eda072', remote_072)]}
        for host, task in tasks.items():
            try:
                records.extend(task.result())
            except Exception as error:
                failures.append(str(error))
                records.extend(dict(row, catalog_warning=str(error)) for row in cached
                               if row.get('host', 'coldsun') == host)
                print(str(error), file=sys.stderr)
    registered_path=ROOT/'amf-registered-records.json'
    if registered_path.is_file():
        records.extend(json.loads(registered_path.read_text())['runs'])
    if not records:
        raise RuntimeError('\n'.join(failures) or 'No recorded layouts found')
    unique = {row['run']: row for row in records}
    return sorted(unique.values(), key=lambda row: row.get('modified', 0), reverse=True)
