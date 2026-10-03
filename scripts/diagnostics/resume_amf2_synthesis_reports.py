#!/usr/bin/env python3
"""Resume reports after a completed post-opt checkpoint; retain failure evidence."""
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from run_amf2_u250_synthesis import digest, save

directory = Path(sys.argv[1]).resolve()
status_path = directory / 'synthesis_status.json'
prior = json.loads(status_path.read_text())
assert prior['state'] == 'failed'
assert 'Overwrite of existing file' in (directory/'console.log').read_text()
for name, metadata in prior['artifacts'].items():
    assert digest(directory/name) == metadata['sha256']
recovery = directory/'report-recovery'
recovery.mkdir(exist_ok=False)
save(recovery/'original_synthesis_status.json', prior)
script = (directory/'synthesize.tcl').read_text()
tail = script[script.index('write_xdc -type timing'):]
(recovery/'reports.tcl').write_text(
    'set_param general.maxThreads 4\nif {[catch {\n'
    + 'open_checkpoint {' + str(directory/'post_opt.dcp') + '}\n' + tail)
command = ['/Projects/Xilinx/Vivado/2024.2/bin/vivado', '-mode', 'batch', '-notrace',
           '-nojournal', '-log', 'vivado.log', '-source', str(recovery/'reports.tcl')]
record = dict(state='running', command=command, started=dt.datetime.now().astimezone().isoformat())
save(recovery/'status.json', record)
start = time.monotonic()
with (recovery/'console.log').open('w') as log:
    code = subprocess.call(command, cwd=recovery, stdout=log, stderr=subprocess.STDOUT)
record.update(state='completed' if code == 0 else 'failed', exit_code=code,
              elapsed_seconds=time.monotonic()-start, finished=dt.datetime.now().astimezone().isoformat())
save(recovery/'status.json', record)
if code:
    sys.exit(code)
for name in ('post_opt_timing.xdc', 'clocks.rpt', 'check_timing.rpt', 'utilization.rpt',
             'timing_summary.rpt', 'synthesis_summary.tsv'):
    target = directory/name
    if target.exists():
        shutil.copy2(target, recovery/('prior-'+name))
    shutil.copy2(recovery/name, target)
prior.update(state='completed', recovered_report_stage=record,
             original_exit_code=prior['exit_code'], exit_code=0,
             original_status=str(recovery/'original_synthesis_status.json'),
             finished=record['finished'], total_elapsed_seconds=prior['elapsed_seconds']+record['elapsed_seconds'])
save(status_path, prior)
print(json.dumps(prior, indent=2))
