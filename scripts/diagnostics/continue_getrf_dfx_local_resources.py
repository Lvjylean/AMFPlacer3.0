#!/usr/bin/env python3
"""One-shot continuation from completed linked DCPs with Pblock-local resource queries."""
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from run_dfx import save, sha


def main():
    evidence = ROOT / 'experiments/evidence/getrf-dfx-large-01/local-resource-01'
    preflight = json.loads((evidence / 'retry-preflight.json').read_text())
    config = Path(preflight['config'])
    output = Path(preflight['run'])
    binary = Path(preflight['binary'])
    assert sha(config) == preflight['config_sha256']
    assert sha(binary) == preflight['binary_sha256']
    for info in preflight['reused_rm_checkpoints'].values():
        assert sha(info['path']) == info['sha256']
    for info in preflight['reused_linked_checkpoints'].values():
        assert sha(info['path']) == info['sha256']
    assert not output.exists(), 'Run already exists; refusing duplicate execution'
    with (evidence / 'started.once').open('x') as handle:
        handle.write(dt.datetime.now().astimezone().isoformat() + '\n')
    command = [sys.executable, str(ROOT / 'scripts/amf3.py'), 'dfx-multi', 'run',
               '--config', str(config), '--output', str(output), '--binary', str(binary)]
    previous = json.loads(Path(preflight['parent_campaign']).read_text())
    jobs = {k: v for k, v in previous['jobs'].items() if k != 'v1_incremental'}
    campaign = dict(state='running', pid=os.getpid(), started_epoch=time.time(),
                    started=dt.datetime.now().astimezone().isoformat(), jobs=jobs,
                    parent_campaign=preflight['parent_campaign'], parent_run=preflight['parent_run'],
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    reuse=preflight['reused_rm_checkpoints'], linked_reuse=preflight['reused_linked_checkpoints'],
                    reason='User requested Pblock-local AMF resource tables; reuse linked DCP without repeated shell link or opt',
                    timing_note='RM synthesis, shell linking and opt are inherited. Interrupted scans, checkpoint copy/reread and resumed execution costs remain separately recorded.')
    with (evidence / 'incremental-launcher.log').open('w') as log:
        child = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        campaign['jobs']['v1_incremental'] = dict(pid=child.pid, command=command, run=str(output),
                                                 state='running', started_epoch=time.time())
        save(evidence / 'campaign.json', campaign)
        save(evidence / 'launch.json', dict(pid=os.getpid(), worker_pid=child.pid, command=command,
                                           campaign=str(evidence / 'campaign.json'), run=str(output)))
        save(evidence.parent / 'recovery-launch.json',
             dict(latest_launch=str(evidence / 'launch.json'), latest_campaign=str(evidence / 'campaign.json'),
                  previous_campaign=preflight['parent_campaign'], reason=campaign['reason']))
        rc = child.wait()
    campaign['state'] = 'completed' if rc == 0 else 'failed'
    campaign['ended_epoch'] = time.time()
    campaign['jobs']['v1_incremental'].update(state=campaign['state'], exit_code=rc, ended_epoch=time.time())
    save(evidence / 'campaign.json', campaign)
    return rc


if __name__ == '__main__':
    raise SystemExit(main())
