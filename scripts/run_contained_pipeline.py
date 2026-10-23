#!/usr/bin/env python3
"""One finite dependency chain, no periodic automation and no automatic retries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from run_contained_base import require_preflight


def save(path, data):
    temp=path.with_suffix('.tmp');temp.write_text(json.dumps(data,indent=2)+'\n');temp.replace(path)


def run(a):
    require_preflight(json.loads(Path(a.base_config).read_text()))
    if a.incremental_config:
        raise ValueError('A preflight-only parent cannot launch incremental implementation; a verified AMF whole-design V0 placement and full route are required.')
    out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False)
    bundle=out/'bundle';bundle.mkdir()
    for f in Path(__file__).resolve().parent.iterdir():
        if f.is_file() and f.suffix in ('.py','.tcl'):shutil.copy2(f,bundle/f.name)
    configs={}
    for label,src in [('base',a.base_config),('incremental',a.incremental_config)]:
        if src:
            configs[label]=bundle/(label+'.json');shutil.copy2(src,configs[label])
    record=dict(state='running',stage='base',pid=os.getpid(),started_epoch=time.time(),
                base_run=a.base_output,incremental_run=a.incremental_output,
                script_hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in bundle.iterdir()},
                steps=[],automatic_retry=False)
    def invoke(label,cmd):
        record['stage']=label;save(out/'status.json',record)
        with (out/(label+'.log')).open('w') as f:
            start=time.time();p=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT,cwd=out)
            record['child_pid']=p.pid;save(out/'status.json',record)
            rc=p.wait()
        record['steps'].append(dict(label=label,command=cmd,exit_code=rc,started_epoch=start,ended_epoch=time.time()))
        if rc:raise RuntimeError(label+' failed; no downstream task launched')
    try:
        invoke('base',[sys.executable,str(bundle/'run_contained_base.py'),'--config',str(configs['base']),'--output',a.base_output])
        if a.incremental_config:
            m=json.loads((Path(a.base_output)/'manifest.json').read_text())
            if m['state']!='completed' or not m['acceptance']['implementation_verified']:
                raise RuntimeError('Parent implementation did not pass')
            if not (Path(a.base_output)/'static_route_audit_counts.tcldata').exists():
                raise RuntimeError('Missing routed static-footprint audit')
            invoke('parallel_incremental',[sys.executable,str(bundle/'run_dfx_multi.py'),'run','--config',str(configs['incremental']),
                                          '--output',a.incremental_output,'--binary',a.binary])
        record['state']='completed';record['stage']='finished'
        record['performance_acceptance']='pending matching V1 reference and evaluate_incremental_budget.py'
    except Exception as exc:
        record['state']='failed';record['error']=str(exc)
    record['ended_epoch']=time.time();save(out/'status.json',record)
    return 0 if record['state']=='completed' else 2


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for field in ('base-config','base-output','output'):p.add_argument('--'+field,required=True)
    for field in ('incremental-config','incremental-output','binary'):p.add_argument('--'+field)
    a=p.parse_args()
    if a.incremental_config and not (a.incremental_output and a.binary):p.error('Incremental output and binary are required')
    raise SystemExit(run(a))
