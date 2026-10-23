#!/usr/bin/env python3
"""Retry failed DFX import/backend in fresh directories without rerunning AMF.

Reuse only a validated successful AMF export; preserve parent failures and timings.
Both routed RMs must pass checks before assembly can start.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from run_dfx import save, sha, tcl_word, placement_rows
from run_dfx_multi import snapshot, settings, vivado, acceptance, check_shell
from summarize_dfx import summarize


def check_worker(source):
    old=json.loads((source/'manifest.json').read_text())
    if old['state']!='failed' or json.loads((source/'status.json').read_text())['state']!='failed':
        raise ValueError('Parent worker has not stopped with failure')
    if not any(s['name']=='amf' and s['exit_code']==0 for s in old['stages']):
        raise ValueError('No successful AMF stage to reuse')
    if not any(s['name']=='backend' and s['exit_code']!=0 for s in old['stages']):
        raise ValueError('Expected a backend failure')
    for key,h in old['input_hashes'].items():
        if sha(old['job'][key])!=h:raise ValueError('Input changed: '+key)
    if sha(old['binary'])!=old['binary_sha256']:raise ValueError('AMF binary changed')
    for name,item in old['input_validation']['files'].items():
        if sha(source/name)!=item['sha256']:raise ValueError('AMF input changed: '+name)
    if sha(source/'linked.dcp')!=old['input_recovery']['linked.dcp']['sha256']:
        raise ValueError('Linked checkpoint changed')
    with (source/'cells.tsv').open() as f:active={r['cell'] for r in csv.DictReader(f,delimiter='\t')}
    rows=placement_rows(source/'placement/DumpCLBPacking-first-0.tcl',active,
        set((source/'allowed_sites.txt').read_text().splitlines()),set((source/'blocked_sites.txt').read_text().splitlines()))
    if (source/'placement.tsv').read_text()!=''.join(n+'\t'+v+'\n' for n,v in sorted(rows.items())):
        raise ValueError('Placement table differs from successful AMF export')
    return old


def run(args):
    parent=args.parent.resolve();out=args.output.resolve()
    previous=json.loads((parent/'manifest.json').read_text())
    if previous['state']!='failed':raise ValueError('Expected a stopped failed parent')
    job=previous['job'];shell,_=check_shell(job)
    old_workers={r['id']:check_worker(parent/'workers'/r['id']) for r in job['partitions']}
    out.mkdir(parents=True,exist_ok=False);scripts=snapshot(out);(out/'workers').mkdir()
    shutil.copy2(__file__,out/'inputs'/Path(__file__).name);scripts[Path(__file__).name]=sha(__file__)
    start=time.time()
    record=dict(schema='amf-dfx-backend-resume-v1',state='running',job=job,pid=os.getpid(),parent_run=str(parent),
        parent_manifest_sha256=sha(parent/'manifest.json'),source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        started_epoch=start,script_hashes=scripts,workers={},stages=[],dcp_storage='server-only',
        prepare_rerun=False,amf_rerun=False,rm_synthesis_rerun=False,
        timing_note='Retry wall time includes backend only; AMF, prepare and failed backend times remain in inherited records.')
    save(out/'manifest.json',record);save(out/'status.json',dict(state='running',stage='parallel_vivado_backends'))

    def worker(r):
        name=r['id'];source=parent/'workers'/name;target=out/'workers'/name;old=old_workers[name]
        target.mkdir();(target/'logs').mkdir();(target/'inputs').mkdir();(target/'reports/physical').mkdir(parents=True)
        started=time.time()
        m=dict(schema='amf-dfx-run-v1',state='running',job=old['job'],stages=[],parent_run=str(source),
            parent_manifest_sha256=sha(source/'manifest.json'),input_hashes=old['input_hashes'],input_recovery={},
            inherited_stages=old['stages'],inherited_prepare_and_prior_attempts=old.get('inherited_stages',[]),
            binary=old['binary'],binary_sha256=old['binary_sha256'],amf_source_commit=old['source_commit'],
            source_commit=record['source_commit'],amf_rerun=False,prepare_rerun=False,
            active_placement_coverage=old['active_placement_coverage'],input_validation=old['input_validation'],
            started_epoch=started,dcp_storage='server-only')
        save(target/'manifest.json',m);save(target/'status.json',dict(state='running',stage='recover_backend_inputs'))
        try:
            files=['linked.dcp','shell_boundary.tcldata','linked_boundary.tcldata','modeled_sites.txt','allowed_sites.txt',
                'blocked_sites.txt','placement.tsv','cells.tsv','static_placement.tsv','input_scope.tsv','amf_config.json']
            if old['job'].get('static_placement_audit','full')=='full':files.append('static_snapshot.tcldata')
            for n in files:
                shutil.copy2(source/n,target/n)
                m['input_recovery'][n]=dict(path=str(source/n),sha256=sha(source/n))
                if sha(target/n)!=m['input_recovery'][n]['sha256']:raise ValueError('Copy mismatch: '+n)
            for n in ('dfx_common.tcl','dfx_backend.tcl','dfx_import.tcl'):shutil.copy2(out/'inputs'/n,target/'inputs'/n)
            config=target/'inputs/settings.tcl';values=dict(old['job'],output=str(target),modeled_sites=str(target/'modeled_sites.txt'))
            config.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in values.items())+'\n')
            m['script_hashes']={p.name:sha(p) for p in (target/'inputs').iterdir()}
            m['input_copy_seconds']=time.time()-started;save(target/'manifest.json',m)
            save(target/'status.json',dict(state='running',stage='backend'))
            step=vivado(job,target,'dfx_backend.tcl',config);step['name']='backend';m['stages'].append(step);save(target/'manifest.json',m)
            if step['exit_code']:raise RuntimeError('Backend failed '+str(step['exit_code']))
            names=['imported.dcp','placed.dcp','routed.dcp','rm_routed.dcp','pr_verify_passed.txt','full.bit','partial.bit']
            if any(not (target/n).is_file() or not (target/n).stat().st_size for n in names):raise RuntimeError('Missing backend artifact')
            m['acceptance']=summarize(target);save(target/'acceptance.json',m['acceptance'])
            if not m['acceptance']['implementation_verified']:raise RuntimeError('Backend acceptance failed')
            m['outputs']={n:dict(path=str(target/n),sha256=sha(target/n),bytes=(target/n).stat().st_size) for n in names};m['state']='completed'
        except Exception as error:m['state']='failed';m['error']=str(error)
        finally:
            m['total_wall_seconds']=time.time()-started;m['ended_epoch']=time.time();save(target/'manifest.json',m)
            save(target/'status.json',dict(state=m['state'],error=m.get('error')))
        return name,dict(state=m['state'],run=str(target),error=m.get('error'))
    try:
        with ThreadPoolExecutor(max_workers=min(2,int(job.get('max_parallel',2)))) as pool:
            for future in as_completed([pool.submit(worker,r) for r in job['partitions']]):
                name,result=future.result();record['workers'][name]=result;save(out/'manifest.json',record)
        if any(w['state']!='completed' for w in record['workers'].values()):raise RuntimeError('Worker failed; see manifests')
        assembly=out/'assembly';assembly.mkdir();(assembly/'inputs').mkdir();(assembly/'logs').mkdir()
        for n in ('dfx_multi_assemble.tcl','dfx_multi_common.tcl','dfx_common.tcl'):shutil.copy2(out/'inputs'/n,assembly/'inputs'/n)
        save(out/'status.json',dict(state='running',stage='assemble_routed_modules'))
        step=vivado(job,assembly,'dfx_multi_assemble.tcl',settings(job,assembly,dict(workers=str(out/'workers'),reference_checkpoint=str(shell/'base_routed.dcp'))));record['stages'].append(step)
        if step['exit_code']:raise RuntimeError('Routed assembly failed')
        record['acceptance']=acceptance(assembly,'assembled')
        record['acceptance']['pr_verify_full_check']=(assembly/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
        if not record['acceptance']['implementation_verified'] or not record['acceptance']['pr_verify_full_check']:raise RuntimeError('Assembly acceptance failed')
        record['outputs']={str(assembly/n):sha(assembly/n) for n in ['assembled_routed.dcp','full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]}
        record['state']='completed'
    except Exception as error:record['state']='failed';record['error']=str(error)
    finally:
        record['wall_seconds']=time.time()-start;record['ended_epoch']=time.time();save(out/'manifest.json',record)
        save(out/'status.json',dict(state=record['state'],error=record.get('error')))
    return 0 if record['state']=='completed' else 1

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--parent',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    raise SystemExit(run(p.parse_args()))
