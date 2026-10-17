#!/usr/bin/env python3
"""Launch the finite A/C/D backend queue using frozen SLL study inputs."""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

STUDY = Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01')
REPO = STUDY / 'repo'
COMPARISON = REPO / 'experiments/comparisons/20261013-sll-acd-backend-01'
MIN_AVAILABLE_GIB = 48  # One full flow; 40 GiB working allowance + 8 GiB reserve.


def now():
    return dt.datetime.now().astimezone().isoformat()


def read(path):
    return json.loads(Path(path).read_text())


def save(path, value):
    path = Path(path)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
    temp.replace(path)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def resources():
    memory = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
    return dict(time=now(), available_gib=int(memory['MemAvailable'].split()[0]) / 1048576,
                load_average=list(os.getloadavg()), free_disk_gib=shutil.disk_usage(REPO).free / 2**30)


def verify(hashes):
    for path, expected in hashes.items():
        if sha(path) != expected:
            raise RuntimeError('Frozen file changed: ' + path)


def prepare():
    assert not COMPARISON.exists(), 'Comparison already exists; do not duplicate runs'
    base = read(STUDY / 'manifest.json')
    inputs = {v['path']: v['sha256'] for v in base['inputs'].values()}
    verify(inputs)
    runner_files = {str(p): sha(p) for p in (REPO / 'scripts').rglob('*')
                    if p.is_file() and p.suffix in ('.py', '.tcl')}
    runner_files[str(STUDY / 'run_case.py')] = sha(STUDY / 'run_case.py')
    definitions = [
        ('A', 'original', 'sll-quantity-baseline.json', 'a-backend-01'),
        ('C', 'release-control-final', 'sll-release-control-final.json', 'c-backend-01'),
        ('D', 'quantity-100-final', 'sll-quantity-100-final.json', 'd-backend-01'),
    ]
    cases = []
    for label, previous, config_name, name in definitions:
        assert not (STUDY / 'cases' / name).exists()
        old_worker = read(STUDY / 'cases' / previous / 'worker.json')
        binary = old_worker['command'][old_worker['command'].index('--binary') + 1]
        assert sha(binary) == old_worker['binary_sha256']
        source = REPO / 'configs/experiments' / config_name
        assert sha(source) == old_worker['config_sha256']
        config = read(source)
        assert config['ProPrimitiveTiming'] == 'false'
        assert config['ClockPeriod'] == '10' and config['jobs'] == '8'
        metadata = Path(config['SRL control metadata file'])
        inputs[str(metadata)] = sha(metadata)
        cases.append(dict(label=label, name=name, previous_gp_case=previous,
                          previous_gp_run=old_worker['runs'][0], binary=binary,
                          binary_sha256=old_worker['binary_sha256'],
                          source_config=str(source), source_config_sha256=sha(source),
                          config_data=config, state='queued'))
    version = subprocess.check_output([base['vivado'], '-version'], text=True,
                                      stderr=subprocess.STDOUT, timeout=60).strip()
    COMPARISON.mkdir(parents=True, exist_ok=False)
    (COMPARISON / 'configs').mkdir()
    for case in cases:
        config = case.pop('config_data')
        original = dict(config)
        if 'SllReportDirectory' in config:
            config['SllReportDirectory'] = str(STUDY / 'cases' / case['name'] / 'sll')
        changes = {k: dict(before=original.get(k), after=v) for k, v in config.items()
                   if original.get(k) != v}
        assert set(changes) <= {'SllReportDirectory'}
        config_path = COMPARISON / 'configs' / (case['label'].lower() + '.json')
        save(config_path, config)
        case.update(config=str(config_path), config_sha256=sha(config_path),
                    config_changes=changes,
                    command=[sys.executable, str(STUDY / 'run_case.py'), case['name'],
                             case['binary'], str(config_path), '--full'])
    protected = {}
    for old in ('original', 'disabled-final', 'release-control-final', 'quantity-100-final'):
        for p in (STUDY / 'cases' / old).rglob('*'):
            if p.is_file() and (p.suffix in ('.json', '.tsv') or p.name == 'launcher.log'):
                protected[str(p)] = sha(p)
    manifest = dict(schema='sll-acd-backend-comparison-v1', created=now(), host='coldsun/eda099',
                    study=str(STUDY), repo=str(REPO), cases=cases, input_hashes=inputs,
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                    git_status=subprocess.check_output(['git', 'status', '--short'], cwd=REPO, text=True),
                    runner_files_sha256=runner_files, protected_previous_reports=protected,
                    vivado_executable=base['vivado'], vivado_version=version,
                    amf_clock_period_ns=10, vivado_clock_source='unchanged frozen DCP; report_clocks recorded per run',
                    amf_threads=8, vivado_threads=4, import_policy='repair',
                    concurrency=1, minimum_available_gib=MIN_AVAILABLE_GIB,
                    scope='Complete AMF including final packing, then Vivado placement/routing and audits',
                    elapsed_policy='Record AMF process wall time and separate Vivado place/route; shared-host load remains uncontrolled',
                    dcp_storage='server-only', initial_resources=resources())
    save(COMPARISON / 'manifest.json', manifest)
    script = COMPARISON / 'launch_backend_comparison.py'
    shutil.copy2(Path(__file__), script)
    with (COMPARISON / 'supervisor.log').open('x') as log:
        child = subprocess.Popen([sys.executable, str(script), 'supervise'], cwd=REPO,
                                 stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
    launch = dict(created=now(), supervisor_pid=child.pid, comparison=str(COMPARISON),
                  script_sha256=sha(script), order=['A', 'C', 'D'], concurrency=1)
    save(COMPARISON / 'launch.json', launch)
    print(json.dumps(launch, indent=2))


def supervise():
    manifest = read(COMPARISON / 'manifest.json')
    state = dict(state='running', started=now(), supervisor_pid=os.getpid(), cases=manifest['cases'])
    save(COMPARISON / 'status.json', state)
    try:
        for case in state['cases']:
            while True:
                sample = resources()
                case.update(state='waiting_for_memory', resources_before_start=sample)
                save(COMPARISON / 'status.json', state)
                if sample['available_gib'] >= MIN_AVAILABLE_GIB and sample['free_disk_gib'] >= 50:
                    break
                time.sleep(30)
            verify(manifest['input_hashes'])
            verify(manifest['runner_files_sha256'])
            verify({case['binary']: case['binary_sha256'], case['config']: case['config_sha256']})
            with (COMPARISON / (case['label'].lower() + '-worker.log')).open('x') as log:
                proc = subprocess.Popen(case['command'], cwd=REPO, stdout=log, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL)
                case.update(state='running', worker_pid=proc.pid, started=now())
                save(COMPARISON / 'status.json', state)
                code = proc.wait()
            case.update(state='completed' if code == 0 else 'failed', exit_code=code, finished=now())
            worker = STUDY / 'cases' / case['name'] / 'worker.json'
            if worker.is_file():
                case['worker'] = read(worker)
                case['runs'] = case['worker'].get('runs', [])
                for run in case['runs']:
                    run = Path(run)
                    if (run / 'reports/summary.json').is_file():
                        save(COMPARISON / (case['label'].lower() + '-summary.json'), read(run / 'reports/summary.json'))
                    if (run / 'status.json').is_file():
                        case['run_status'] = read(run / 'status.json')
            verify(manifest['protected_previous_reports'])
            save(COMPARISON / 'status.json', state)
        state.update(state='completed' if all(c['exit_code'] == 0 for c in state['cases']) else 'completed_with_failures',
                     finished=now())
    except Exception as error:
        state.update(state='failed', error=str(error), finished=now())
        raise
    finally:
        save(COMPARISON / 'status.json', state)


if __name__ == '__main__':
    if sys.argv[1:] == ['prepare']:
        prepare()
    elif sys.argv[1:] == ['supervise']:
        supervise()
    else:
        raise SystemExit('Use prepare once; supervise is launched by prepare')
