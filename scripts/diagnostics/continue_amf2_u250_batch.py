#!/usr/bin/env python3
"""One-shot dependency runner for the currently authorized three-case batch."""
import datetime as dt
import json
from pathlib import Path
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
catalog=Path(sys.argv[1]).resolve()
output=Path(sys.argv[2]).resolve()
items=json.loads(catalog.read_text())
status_path=catalog.parent/'continuation_status.json'
state=dict(state='waiting_for_netlists',started=dt.datetime.now().astimezone().isoformat(),catalog=str(catalog),full_batch=str(output),launched=[])

def save():
    temp=status_path.with_suffix('.tmp')
    temp.write_text(json.dumps(state,indent=2)+'\n')
    temp.replace(status_path)

save()
try:
    while True:
        readiness=[]
        for item in items:
            directory=Path(item['directory'])
            synthesis_status=directory/'synthesis_status.json'
            if not synthesis_status.exists():
                readiness.append(False)
                continue
            synthesis=json.loads(synthesis_status.read_text())
            if synthesis['state']=='failed':
                raise RuntimeError('Synthesis failed: '+item['case'])
            prep=directory/item.get('preparation_tag','amf-preparation')
            if synthesis['state']=='completed' and not prep.exists():
                log=directory/'continuation_preparation.log'
                with log.open('x') as f:
                    command=['python3','-u','scripts/diagnostics/prepare_amf2_core_netlists.py',str(catalog),'--case',item['case']]
                    process=subprocess.Popen(command,cwd=root,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT)
                state['launched'].append(dict(case=item['case'],pid=process.pid,command=command))
                save()
                # Prevent a second launch while the child creates its directory.
                for unused in range(20):
                    if prep.exists() or process.poll() is not None:break
                    time.sleep(1)
                if not prep.exists():raise RuntimeError('Preparation launch failed: '+item['case'])
            p=prep/'status.json'
            if not p.exists():readiness.append(False);continue
            current=json.loads(p.read_text())
            if current['state']=='failed':raise RuntimeError('Input preparation failed: '+item['case']+': '+current.get('error',''))
            readiness.append(current['state']=='completed')
        if all(readiness):break
        time.sleep(15)
    state['state']='running_full_batch'
    command=['python3','-u','scripts/diagnostics/run_amf2_u250_full_batch.py',str(catalog),str(output)]
    state['full_batch_command']=command
    save()
    code=subprocess.call(command,cwd=root)
    state.update(state='completed' if code==0 else 'failed',exit_code=code,finished=dt.datetime.now().astimezone().isoformat())
    save()
    sys.exit(code)
except Exception as error:
    state.update(state='failed',error=str(error),finished=dt.datetime.now().astimezone().isoformat())
    save()
    raise
