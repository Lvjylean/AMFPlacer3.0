#!/usr/bin/env python3
"""Finite AMF full-V0 → parallel incremental-V1 → routed assembly chain."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from run_dfx_multi import load
from run_dfx import save, sha


def run(a):
    base=load(a.base_config);inc=load(a.incremental_config)
    if Path(inc['shell_run']).resolve()!=Path(a.base_output).resolve():
        raise ValueError('Incremental parent must be this AMF full V0 output')
    for key in ('part','clock_name','clock_period_ns','static_routing_containment'):
        if base[key]!=inc[key]:raise ValueError('Parent/worker mismatch: '+key)
    out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=False)
    bundle=out/'bundle';bundle.mkdir()
    for f in Path(__file__).resolve().parent.iterdir():
        if f.is_file() and f.suffix in ('.py','.tcl'):shutil.copy2(f,bundle/f.name)
    save(bundle/'base.json',base);save(bundle/'incremental.json',inc)
    m=dict(state='running',started_epoch=time.time(),steps=[],automatic_retry=False,
           base_run=str(Path(a.base_output).resolve()),incremental_run=str(Path(a.incremental_output).resolve()),
           binary=str(Path(a.binary).resolve()),binary_sha256=sha(a.binary),
           frozen_inputs={f.name:sha(f) for f in bundle.iterdir()})
    def invoke(label,cmd):
        m['stage']=label;save(out/'status.json',m);begin=time.time()
        with (out/(label+'.log')).open('w') as log:
            p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,cwd=out)
            m['child_pid']=p.pid;save(out/'status.json',m);rc=p.wait()
        m['steps'].append(dict(name=label,command=cmd,exit_code=rc,started_epoch=begin,ended_epoch=time.time()))
        if rc:raise RuntimeError(label+' failed; downstream work was not launched')
    try:
        invoke('whole_design_amf_v0',[sys.executable,str(bundle/'run_dfx_full.py'),'--config',str(bundle/'base.json'),
                                    '--output',m['base_run'],'--binary',m['binary']])
        parent=json.loads((Path(m['base_run'])/'manifest.json').read_text())
        if parent['state']!='completed' or parent.get('amf_placement_scope')!='whole_design' or not parent['acceptance']['implementation_verified']:
            raise RuntimeError('Unverified AMF full V0 parent')
        invoke('parallel_incremental_v1',[sys.executable,str(bundle/'run_dfx_multi.py'),'run','--config',str(bundle/'incremental.json'),
                                         '--output',m['incremental_run'],'--binary',m['binary']])
        m['state']='completed'
        m['performance_acceptance']='requires matching full V1 AMF reference and evaluate_incremental_budget.py'
    except Exception as exc:
        m['state']='failed';m['error']=str(exc)
    m['ended_epoch']=time.time();m['wall_seconds']=m['ended_epoch']-m['started_epoch'];save(out/'status.json',m)
    return 0 if m['state']=='completed' else 2


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base-config','incremental-config','base-output','incremental-output','output','binary'):p.add_argument('--'+key,required=True)
    raise SystemExit(run(p.parse_args()))
