#!/usr/bin/env python3
"""Assemble successful multi-RP workers without rerunning AMF or routing."""
import argparse
import datetime as dt
import json
from pathlib import Path
import subprocess
import time
from run_dfx_multi import load, check_shell, snapshot, settings, vivado, acceptance
from run_dfx import ROOT, save, sha


def run(args):
    parent=Path(args.run).resolve(); old=json.loads((parent/'manifest.json').read_text())
    job=old['job'];shell,shell_manifest=check_shell(job)
    workers=parent/'workers';worker_hashes={}
    for r in job['partitions']:
        name=r['id'];w=workers/name;m=json.loads((w/'manifest.json').read_text())
        if m['state']!='completed' or not m['acceptance']['implementation_verified']:raise ValueError('Incomplete worker '+name)
        for n,record in m['outputs'].items():
            if sha(w/n)!=record['sha256']:raise ValueError('Changed worker output '+name+'/'+n)
        expected=old['workers'][name]['rm_sha256']
        if sha(w/'rm_routed.dcp')!=expected:raise ValueError('Changed routed module '+name)
        worker_hashes[name]=dict(manifest_sha256=sha(w/'manifest.json'),rm_sha256=expected)
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    scripts=snapshot(out);start=time.monotonic()
    m=dict(schema='amf-dfx-multi-assembly-resume-v1',state='running',job=job,parent_run=str(parent),
           parent_manifest_sha256=sha(parent/'manifest.json'),worker_hashes=worker_hashes,
           source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
           script_hashes=scripts,started=dt.datetime.now().astimezone().isoformat(),
           amf_rerun=False,routing_rerun=False,dcp_storage='server-only')
    save(out/'manifest.json',m);save(out/'status.json',dict(state='running'))
    try:
        config=settings(job,out,dict(workers=str(workers),reference_checkpoint=str(shell/'base_routed.dcp')))
        m['stage']=vivado(job,out,'dfx_multi_assemble.tcl',config)
        if m['stage']['exit_code']:raise RuntimeError('Assembly failed; see logs')
        m['acceptance']=acceptance(out,'assembled')
        m['acceptance']['pr_verify_full_check']=(out/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
        if not m['acceptance']['implementation_verified'] or not m['acceptance']['pr_verify_full_check']:raise RuntimeError('Assembly acceptance failed')
        names=['assembled_routed.dcp','full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]
        m['outputs']={n:dict(sha256=sha(out/n),bytes=(out/n).stat().st_size) for n in names}
        m['state']='completed'
    except Exception as error:
        m['state']='failed';m['error']=str(error);raise
    finally:
        m['wall_seconds']=time.monotonic()-start;save(out/'manifest.json',m);save(out/'status.json',dict(state=m['state'],error=m.get('error')))
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',required=True);p.add_argument('--output',required=True)
    print(run(p.parse_args()))
