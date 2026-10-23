"""Adopt this experiment's running V0; start an independent fresh V1 once."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from run_dfx import save, sha
from parallel_runtime import require_memory, verify_hashes


def process_identity(pid):
    try:
        root = Path('/proc') / str(pid)
        fields = (root / 'stat').read_text().rsplit(')', 1)[1].split()
        return dict(pid=pid, start_ticks=int(fields[19]), state=fields[0],
                    ppid=int(fields[1]), pgid=int(fields[2]),
                    argv=(root / 'cmdline').read_bytes().decode().rstrip('\0').split('\0'))
    except FileNotFoundError:
        return None


def same_process(expected, actual):
    return bool(actual and actual['state'] != 'Z'
                and actual['start_ticks'] == expected['start_ticks']
                and actual['argv'] == expected['argv'])


def observe_adopted(manifest, expected, actual):
    # The producer's final manifest, not process disappearance, certifies completion.
    if same_process(expected, actual):
        return 'running'
    if manifest.get('state') in ('completed', 'failed'):
        return manifest['state']
    return 'lost_producer_without_final_manifest'


def run_full_pair(deployment, out, plan, state):
    deployment, out = Path(deployment), Path(out)
    handoff = json.loads(Path(plan['adoption_record']).read_text())
    if not handoff.get('old_controller_exited'):
        raise RuntimeError('The previous scheduler has not relinquished ownership')
    if same_process(handoff['old_controller'], process_identity(handoff['old_controller']['pid'])):
        raise RuntimeError('Previous scheduler is still alive; refuse duplicate V1')
    expected = handoff['v0_runner']
    v0 = Path(plan['runs']['v0-full'])
    initial = json.loads((v0 / 'manifest.json').read_text())
    if observe_adopted(initial, expected, process_identity(expected['pid'])) != 'running':
        raise RuntimeError('V0 changed state during handoff; inspect before dispatch')
    if Path(plan['runs']['v1-full']).exists():
        raise RuntimeError('V1 output already exists; never duplicate or overwrite')
    memory = require_memory(80)
    verify_hashes({str(deployment / k): v for k, v in plan['files'].items()})
    job = json.loads((deployment / 'configs/v1-full.json').read_text())
    env = dict(os.environ, LD_LIBRARY_PATH=job['amf_runtime_lib'] + ':' + os.environ.get('LD_LIBRARY_PATH', ''))
    command = [sys.executable, str(deployment / 'scripts/run_dfx_full.py'),
               '--config', str(deployment / 'configs/v1-full.json'),
               '--output', plan['runs']['v1-full'], '--binary', job['amf_binary']]
    adopted = dict(name='v0-full', state='running', adopted=True, pid=expected['pid'],
                   started_epoch=handoff['v0_started_epoch'], adoption_epoch=time.time(),
                   original_chain=handoff['old_chain'], original_command=expected['argv'])
    launched = dict(name='v1-full', state='starting', command=command, started_epoch=time.time())
    state.update(state='v0-v1-full-parallel', steps=[adopted, launched],
                 scheduling=dict(mode='independent_full_runs_overlap', initial_memory=memory,
                                 timing_caveat='Same-host parallel load; timings are not isolated benchmarks.',
                                 v0_compute_interrupted=False, automation='PAUSED'))
    save(out / 'manifest.json', state)
    results = {}
    with (out / 'v1-full.log').open('w') as log:
        child = subprocess.Popen(command, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
        launched.update(pid=child.pid, state='running')
        save(out / 'manifest.json', state)
        while len(results) < 2:
            if 'v0-full' not in results:
                m0 = json.loads((v0 / 'manifest.json').read_text())
                observed = observe_adopted(m0, expected, process_identity(expected['pid']))
                if observed != 'running':
                    adopted.update(state=observed, ended_epoch=m0.get('ended_epoch', time.time()),
                                   completion_observed_epoch=time.time(),
                                   exit_code=None, exit_code_note='Non-child producer; final manifest is the completion evidence.')
                    adopted['wall_seconds'] = adopted['ended_epoch'] - adopted['started_epoch']
                    results['v0-full'] = m0 if observed == 'completed' else dict(state=observed, error=m0.get('error'))
                    adopted['producer_manifest_sha256'] = sha(v0 / 'manifest.json')
            code = child.poll()
            if 'v1-full' not in results and code is not None:
                launched.update(exit_code=code, ended_epoch=time.time(), state='completed' if code == 0 else 'failed')
                launched['wall_seconds'] = launched['ended_epoch'] - launched['started_epoch']
                path = Path(plan['runs']['v1-full']) / 'manifest.json'
                m1 = json.loads(path.read_text()) if path.exists() else dict(state='failed', error='Producer manifest absent')
                results['v1-full'] = m1 if code == 0 else dict(state='failed', error=m1.get('error'), exit_code=code)
            state['last_observed_epoch'] = time.time()
            save(out / 'manifest.json', state)
            if len(results) < 2:
                time.sleep(10)
    overlap = max(0, min(adopted['ended_epoch'], launched['ended_epoch']) - max(adopted['started_epoch'], launched['started_epoch']))
    state['scheduling']['full_run_overlap_seconds'] = overlap
    state['scheduling']['barrier_observed_epoch'] = time.time()
    save(out / 'manifest.json', state)
    failures = {k: v for k, v in results.items() if v.get('state') != 'completed'}
    if failures:
        raise RuntimeError('Full-run barrier failed; no incremental dispatch: ' + json.dumps(failures))
    return results
