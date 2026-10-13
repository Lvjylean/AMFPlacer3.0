#!/usr/bin/env python3
"""Retry a recorded original backend without rerunning or changing AMF placement."""
import datetime as dt
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from inspect_amf_inputs import digest
from summarize_amf2_original_vu095 import summarize


def save(path,value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)


def main():
    batch=Path(sys.argv[1]).resolve();name=sys.argv[2]
    item=next(x for x in json.loads((batch/'catalog.json').read_text()) if x['case']==name)
    amf=next(x for x in json.loads((batch/'amf_status.json').read_text())['cases'] if x['case']==name)
    if amf['state']!='completed':raise RuntimeError('AMF placement is incomplete')
    audit=json.loads((batch/'reports'/(name+'-connectivity')/'comparison-canonical.json').read_text())
    cfg=json.loads(Path(item['config']).read_text())
    if audit['state']!='matched' or audit['dcp_binding']['input_sha256']!=digest(Path(item['dcp'])) or audit['input_netlist_sha256']!=digest(Path(cfg['vivado extracted design information file'])):
        raise RuntimeError('Connectivity audit does not match current inputs')
    status=batch/'backend_retries.json'
    records=json.loads(status.read_text()) if status.exists() else []
    if any(x['state']=='running' for x in records):raise RuntimeError('A backend retry is already active')
    attempt=1+sum(x['case']==name for x in records)
    command=['python3','scripts/amf3.py','full-run','--run-prefix',f'amf2-original-{name}-vu095-backend-retry{attempt}',
        '--binary',item['binary'],'--config',item['config'],'--dcp',item['dcp'],
        '--input-provenance',str(batch/'inputs'/(name+'_backend_provenance.json')),
        '--placement-run',amf['run_directory'],'--upstream-backend','--allow-import-repair']
    record=dict(case=name,attempt=attempt,state='running',command=command,
        reason='Restore original literal cell names in audit after Tcl list unescaping; placement commands unchanged',
        started=dt.datetime.now().astimezone().isoformat())
    records.append(record);save(status,records)
    log_path=batch/'logs'/f'{name}-backend-retry{attempt}.log'
    begin=time.monotonic()
    with log_path.open('w') as log:
        process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        record['pid']=process.pid;save(status,records)
        # The recorded entry point announces the new directory before AMF or Vivado.
        for _ in range(60):
            lines=log_path.read_text().splitlines()
            run=next((line for line in lines if line.startswith(str(ROOT/'experiments/runs'))),None)
            if run or process.poll() is not None:break
            time.sleep(1)
        if run:record['run_directory']=run
        record['log_path']=str(log_path);save(status,records)
        save(batch/'reports/results.json',summarize(batch))
        code=process.wait()
    record.update(state='completed' if code==0 else 'failed',exit_code=code,
        elapsed_seconds=time.monotonic()-begin,finished=dt.datetime.now().astimezone().isoformat())
    save(status,records);save(batch/'reports/results.json',summarize(batch))
    print(json.dumps(record),flush=True)
    return code


if __name__=='__main__':sys.exit(main())
