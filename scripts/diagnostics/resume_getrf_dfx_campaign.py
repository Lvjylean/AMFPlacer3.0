#!/usr/bin/env python3
"""One-shot recovery from GETRF synthesis checkpoints; no automatic retries."""
import datetime
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from run_dfx import sha, save
from run_dfx_multi import load, check_synthesis_resume

def main(attempt):
    if attempt < 2:
        raise ValueError('Recovery attempt must be at least 2')
    EVIDENCE = ROOT / f'experiments/evidence/getrf-dfx-large-01/attribute-scan-recovery-{attempt-1:02d}'
    previous = json.loads((EVIDENCE.parent / 'campaign.json').read_text())
    assert previous['state'] == 'failed', 'Original campaign must have exited'
    assert not (EVIDENCE / 'campaign.json').exists(), 'Recovery already launched'
    binary = Path(previous['binary'])
    assert sha(binary) == previous['binary_sha256']
    jobs = {
        'v0': (f'getrf-dfx-v0-full-resume-{attempt:02d}.json', f'getrf-dfx-v0-shell-resume-{attempt:02d}', 'shell'),
        'v1_full': (f'getrf-dfx-v1-full-resume-{attempt:02d}.json', f'getrf-dfx-v1-full-resume-{attempt:02d}', 'shell'),
        'v1_incremental': (f'getrf-dfx-v1-incremental-resume-{attempt:02d}.json', f'getrf-dfx-v1-parallel-{attempt:02d}', 'run'),
    }
    recovery = {}
    for label, (config, name, mode) in jobs.items():
        assert not (ROOT / 'experiments/runs' / name).exists(), name
        if mode == 'shell':
            recovery[label] = check_synthesis_resume(load(ROOT / 'configs/experiments' / config))
    available = int(next(line.split()[1] for line in Path('/proc/meminfo').read_text().splitlines()
                         if line.startswith('MemAvailable:'))) / 1024**2
    assert available >= 70, 'Insufficient available memory for two full contexts'
    record = dict(state='running', pid=os.getpid(), host=os.uname().nodename,
                  started_epoch=time.time(), started=datetime.datetime.now().astimezone().isoformat(),
                  parent_campaign=str(EVIDENCE.parent / 'campaign.json'),
                  source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  binary=str(binary), binary_sha256=sha(binary), resource_available_gib=available,
                  recovery=recovery, jobs={}, reason='Replace per-cell property enumeration with equivalent batch bblock filter',
                  timing_note='Synthesis inherited from parent; retain failed scan, recovery overhead and total campaign wall time separately')
    active = {}

    def update():
        save(EVIDENCE / 'campaign.json', record)

    def start(label):
        config, name, mode = jobs[label]
        out = ROOT / 'experiments/runs' / name
        cmd = [sys.executable, str(ROOT / 'scripts/amf3.py'), 'dfx-multi', mode,
               '--config', str(ROOT / 'configs/experiments' / config), '--output', str(out)]
        if mode == 'run':
            cmd += ['--binary', str(binary)]
        with (EVIDENCE / (label + '-launcher.log')).open('xb') as log:
            process = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        active[label] = process
        record['jobs'][label] = dict(pid=process.pid, command=cmd, run=str(out), state='running', started_epoch=time.time())
        update()

    def wait_jobs():
        while active:
            for label, process in list(active.items()):
                code = process.poll()
                if code is not None:
                    record['jobs'][label].update(state='completed' if code == 0 else 'failed',
                                                 exit_code=code, ended_epoch=time.time())
                    del active[label]
                    update()
            if active:
                time.sleep(20)

    update()
    start('v0')
    start('v1_full')
    wait_jobs()
    if record['jobs']['v0']['exit_code'] == 0:
        start('v1_incremental')
        wait_jobs()
    else:
        record['incremental_blocked'] = 'Recovered V0 shell failed; retain completed artifacts.'
    record.update(state='completed' if len(record['jobs']) == 3 and all(v['exit_code'] == 0 for v in record['jobs'].values()) else 'failed',
                  ended_epoch=time.time())
    record['recovery_wall_seconds'] = record['ended_epoch'] - record['started_epoch']
    record['campaign_wall_seconds_including_interruption'] = record['ended_epoch'] - previous['started_epoch']
    update()
    return 0 if record['state'] == 'completed' else 2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=int, default=2)
    sys.exit(main(parser.parse_args().attempt))
