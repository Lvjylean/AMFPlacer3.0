#!/usr/bin/env python3
"""Reuse a recorded successful AMF DFX placement in a new backend-only run."""
import argparse
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import time
from run_dfx import ROOT, SCRIPT, save, sha, tcl_word
from summarize_dfx import summarize


def run(args):
    parent=Path(args.run).resolve()
    previous=json.loads((parent/'manifest.json').read_text())
    if previous.get('schema')!='amf-dfx-run-v1' or not any(s['name']=='amf' and s['exit_code']==0 for s in previous['stages']):
        raise ValueError('Parent must have a recorded successful DFX AMF stage')
    job=previous['job']
    for k,h in previous['input_hashes'].items():
        if sha(job[k])!=h:raise ValueError('Parent input changed: '+k)
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'inputs').mkdir();(out/'logs').mkdir()
    names=('linked.dcp','placement.tsv','cells.tsv','allowed_sites.txt','blocked_sites.txt',
           'static_snapshot.tcldata','shell_boundary.tcldata','linked_boundary.tcldata')
    inputs={}
    for n in names:
        shutil.copy2(parent/n,out/n);inputs[n]=sha(out/n)
    for n in ('dfx_backend.tcl','dfx_common.tcl','resume_dfx.py','run_dfx.py','summarize_dfx.py','summarize_full_flow.py','summarize_face_detect_flow.py'):
        shutil.copy2(SCRIPT/n,out/'inputs'/n)
    settings=dict(job,output=str(out))
    setting=out/'inputs/settings.tcl';setting.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in settings.items())+'\n')
    record=dict(schema='amf-dfx-backend-resume-v1',state='running',job=job,parent_run=str(parent),
                parent_manifest_sha256=sha(parent/'manifest.json'),amf_reused=True,
                reused_amf_seconds=next(s['seconds'] for s in previous['stages'] if s['name']=='amf'),
                active_placement_coverage=previous['active_placement_coverage'],
                linked_input_hashes=inputs,script_hashes={p.name:sha(p) for p in (out/'inputs').iterdir()},
                source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                started=dt.datetime.now().astimezone().isoformat(),dcp_storage='server-only')
    save(out/'manifest.json',record);save(out/'status.json',dict(state='running',stage='backend'));t=time.monotonic()
    try:
        cmd=[job['vivado'],'-mode','batch','-source',str(out/'inputs/dfx_backend.tcl'),'-tclargs',str(setting)]
        with (out/'logs/backend.log').open('w') as f:r=subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT)
        record['command']=cmd;record['exit_code']=r.returncode
        if r.returncode:raise RuntimeError('DFX backend failed; see '+str(out/'logs/backend.log'))
        record['acceptance']=summarize(out);save(out/'acceptance.json',record['acceptance'])
        if not record['acceptance']['implementation_verified']:raise RuntimeError('DFX implementation acceptance failed')
        record['outputs']={n:dict(path=str(out/n),sha256=sha(out/n),bytes=(out/n).stat().st_size)
                           for n in ('imported.dcp','placed.dcp','routed.dcp','pr_verify_passed.txt','full.bit','partial.bit')}
        record['state']='completed'
    except Exception as e:
        record['state']='failed';record['error']=str(e);raise
    finally:
        record['total_wall_seconds']=time.monotonic()-t;save(out/'manifest.json',record)
        save(out/'status.json',dict(state=record['state'],error=record.get('error')))
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',required=True);p.add_argument('--output',required=True)
    print(run(p.parse_args()))
