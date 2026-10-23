"""Isolated process execution and an all-success barrier; no shared Vivado state."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import json
import os
import signal
import subprocess
import time
from run_dfx import save, sha


def require_memory(minimum_gib):
    p = Path('/proc/meminfo')
    if not p.is_file():
        raise RuntimeError('Native scheduling requires Linux memory accounting')
    values = dict(line.split(':', 1) for line in p.read_text().splitlines())
    available = int(values['MemAvailable'].split()[0]) * 1024
    if available < minimum_gib * 2**30:
        raise RuntimeError(f'Insufficient memory for isolated parallel processes: {available / 2**30:.2f} GiB')
    return dict(available_bytes=available, required_gib=minimum_gib)


def command(argv, out, env=None, timeout=14400):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    result = dict(command=list(map(str, argv)), cwd=str(out), started_epoch=time.time(), state='starting')
    with (out/'process.log').open('w') as log:
        p = subprocess.Popen(result['command'], cwd=out, env=env, stdout=log,
                             stderr=subprocess.STDOUT, start_new_session=True)
        result.update(pid=p.pid, process_group=p.pid, state='running')
        save(out/'process.json', result)
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Only the newly created, privately owned process group is touched.
            os.killpg(p.pid, signal.SIGTERM)
            try:
                code = p.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                code = p.wait()
            result['timed_out'] = True
        result.update(exit_code=code, ended_epoch=time.time(), seconds=time.monotonic()-start,
                      state='completed' if code == 0 else 'failed')
        save(out/'process.json', result)
    if code:
        raise RuntimeError(f'Command failed ({code}); preserved evidence: {out}')
    return result


def parallel_barrier(tasks, workers=2):
    """Collect every result, including failures, and never continue on partial success."""
    if not tasks or workers < 1:
        raise ValueError('Empty or invalid parallel task set')
    results = {}
    failures = {}
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(fn): name for name, fn in tasks.items()}
        for future in as_completed(futures):
            name = futures[future]
            try:
                results[name] = future.result()
            except Exception as error:
                failures[name] = repr(error)
    if failures:
        raise RuntimeError('Parallel barrier failed; successful siblings retained: '+json.dumps(failures))
    return results


def verify_hashes(expected):
    for path, digest in expected.items():
        if sha(path) != digest:
            raise ValueError('Immutable input changed: '+str(path))
