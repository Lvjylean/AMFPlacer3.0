#!/usr/bin/env python3
"""Parallel per-RP AMF + Vivado DFX implementation, followed by routed assembly.

Workers use independent full-context shells derived from one immutable parent.
AMF receives only the active RP and its fixed interface timing cones.
Vivado retains the full shell; this does not claim Abstract Shell support or exact AMF boundary budgets.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import datetime as dt
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from run_dfx import ROOT, SCRIPT, save, sha, tcl_word, amf_configuration
from summarize_face_detect_flow import timing, drc
from summarize_full_flow import routing_complete


def validate(job):
    if job.get('IncremenralCompilation') not in (True, 'true'):
        raise ValueError('Explicit IncremenralCompilation required')
    rps = job.get('partitions')
    if not isinstance(rps, list) or len(rps) < 2:
        raise ValueError('At least two partitions required')
    for key in ('id', 'active_partition', 'pblock'):
        values = [r[key] for r in rps]
        if len(set(values)) != len(values): raise ValueError('Duplicate '+key)
    for r in rps:
        if not re.fullmatch(r'[a-zA-Z][a-zA-Z0-9_-]*', r['id']): raise ValueError('Unsafe partition id')
        rp = r['active_partition']
        if not rp or rp.endswith('/') or any(c.isspace() for c in rp): raise ValueError('Invalid RP path')
        for other in rps:
            if other is not r and other['active_partition'].startswith(rp+'/'):
                raise ValueError('Nested RPs are not supported by this parallel flow')
        ranges=r.get('pblock_ranges')
        if not isinstance(ranges,list) or not ranges or any(not re.fullmatch(r'[A-Z0-9_]+_X[0-9]+Y[0-9]+:[A-Z0-9_]+_X[0-9]+Y[0-9]+',v) for v in ranges):
            raise ValueError('Explicit Pblock ranges required')
    if not 0 < float(job['clock_period_ns']) < 10000: raise ValueError('Invalid clock period')
    if int(job.get('max_parallel', 2)) < 1: raise ValueError('Invalid parallel budget')
    if job.get('static_placement_audit', 'full') not in ('full', 'skip'):
        raise ValueError('Invalid static_placement_audit mode')
    return job


def load(path):
    path=Path(path).resolve(); job=validate(json.loads(path.read_text()))
    for k in ('vivado', 'amf_config', 'shell_run', 'resume_synthesis_run'):
        if k in job: job[k]=str((path.parent/job[k]).resolve())
    for k in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files'):
        if k in job: job[k]=[str((path.parent/v).resolve()) for v in job[k]]
    for r in job['partitions']:
        if 'rm_checkpoint' in r: r['rm_checkpoint']=str((path.parent/r['rm_checkpoint']).resolve())
        if 'prepare_from_run' in r: r['prepare_from_run']=str((path.parent/r['prepare_from_run']).resolve())
        for k in ('rm_rtl_sources','rm_ip_scripts'):
            if k in r: r[k]=[str((path.parent/v).resolve()) for v in r[k]]
    job.setdefault('vivado_threads',4); job.setdefault('max_parallel',2)
    job.setdefault('require_distinct_slrs',False); job.setdefault('assembly_xdc_files',[])
    return job


def settings(job, out, extra=None):
    values={k:v for k,v in job.items() if not isinstance(v,(list,dict))}
    values.update(output=str(out), require_distinct_slrs=int(job['require_distinct_slrs']))
    for k in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files'):
        values[k]=' '.join(tcl_word(v) for v in job.get(k,[]))
    rows=[]
    for r in job['partitions']:
        row=[r['id'],r['active_partition'],r['pblock'],' '.join(r['pblock_ranges']),r.get('expected_slr','')]
        rows.append(tcl_word(' '.join(tcl_word(v) for v in row)))
    values['rps']=' '.join(rows)
    if extra: values.update(extra)
    dest=out/'inputs/settings.tcl'
    dest.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in values.items())+'\n')
    return dest


def snapshot(out):
    (out/'inputs').mkdir(); (out/'logs').mkdir()
    names=('run_dfx_multi.py','resume_dfx_multi.py','dfx_multi_shell.tcl','dfx_rm_synth.tcl','dfx_multi_assemble.tcl','dfx_multi_common.tcl','dfx_common.tcl',
           'run_dfx.py','dfx_prepare.tcl','dfx_input_scope.tcl','dfx_backend.tcl','summarize_dfx.py','summarize_full_flow.py','summarize_face_detect_flow.py')
    for n in names: shutil.copy2(SCRIPT/n,out/'inputs'/n)
    return {n:sha(out/'inputs'/n) for n in names}


def vivado(job,out,script,config):
    start=time.time(); t=time.monotonic()
    cmd=[job['vivado'],'-mode','batch','-source',str(out/'inputs'/script),'-tclargs',str(config)]
    with (out/'logs'/f'{script}.log').open('w') as f:
        result=subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT)
    return dict(command=cmd,seconds=time.monotonic()-t,started_epoch=start,ended_epoch=time.time(),exit_code=result.returncode)


def acceptance(out,prefix):
    counts={}
    for line in (out/(prefix+'_route_status.rpt')).read_text().splitlines():
        m=re.search(r'# of\s+(.+?)\s*:\s*(\d+)',line)
        if m: counts[m[1].rstrip('. ')]=int(m[2])
    d={k:int(v) for k,v in (x.split('\t') for x in (out/(prefix+'_drc_counts.tsv')).read_text().splitlines())}
    tm=timing(out/(prefix+'_timing.rpt'))
    return dict(routing_counts=counts,routing_complete=routing_complete(counts),drc=d,timing=tm,
                implementation_verified=routing_complete(counts) and not any(d.values()),
                timing_met=all(tm[k]>=0 for k in ('wns_ns','whs_ns','wpws_ns')))


def check_shell(job):
    shell=Path(job['shell_run']); m=json.loads((shell/'manifest.json').read_text())
    if m['state']!='completed' or m['schema']!='amf-dfx-multi-shell-v1': raise ValueError('Invalid multi-RP parent')
    for k in ('part','clock_name','clock_period_ns'):
        if job[k]!=m['job'][k]: raise ValueError('Parent mismatch: '+k)
    for k in ('id','active_partition','pblock','pblock_ranges','expected_slr'):
        if [r.get(k) for r in job['partitions']] != [r.get(k) for r in m['job']['partitions']]: raise ValueError('Parent partition mismatch: '+k)
    for n,h in m['outputs'].items():
        if sha(shell/n)!=h: raise ValueError('Parent artifact changed: '+n)
    return shell,m


def check_synthesis_resume(job):
    """Reuse a completed synthesis stage only with identical design inputs."""
    parent=Path(job['resume_synthesis_run'])
    old=json.loads((parent/'manifest.json').read_text())
    if old.get('schema')!='amf-dfx-multi-shell-v1': raise ValueError('Invalid synthesis parent')
    for key in ('part','top','clock_name','clock_period_ns','partitions','vivado','vivado_threads','rtl_sources','ip_scripts','xdc_files','prepare_worker_shells'):
        if old['job'].get(key)!=job.get(key): raise ValueError('Synthesis parent mismatch: '+key)
    current={f:sha(f) for k in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files') for f in job.get(k,[])}
    if current!=old['input_hashes']: raise ValueError('Synthesis input hashes changed')
    stages=[line.split('\t') for line in (parent/'stage_times.tsv').read_text().splitlines()]
    for stage in ('synthesis','write_synth_dcp'):
        if not any(s[0]==stage and s[2]=='0' for s in stages): raise ValueError('Incomplete '+stage)
    checkpoint=parent/'base_synth.dcp'
    if not checkpoint.is_file(): raise ValueError('Missing synthesis checkpoint')
    job['resume_synthesis_checkpoint']=str(checkpoint)
    return dict(parent_run=str(parent),parent_manifest_sha256=sha(parent/'manifest.json'),
                checkpoint=str(checkpoint),checkpoint_sha256=sha(checkpoint),
                inherited_stage_seconds={s[0]:float(s[1]) for s in stages if s[2]=='0'},
                synthesis_rerun=False,timing_note='Resume wall time excludes original synthesis and interrupted metadata scan; report separately')


def run(args):
    job=load(args.config); out=Path(args.output).resolve()
    # Validate external inputs before creating work or dispatching processes.
    for k in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files'):
        for f in job.get(k,[]):
            if not Path(f).is_file(): raise ValueError('Missing input '+f)
    recovery=None
    if 'resume_synthesis_checkpoint' in job: raise ValueError('Use resume_synthesis_run for validated recovery')
    if 'resume_synthesis_run' in job:
        if args.mode!='shell': raise ValueError('Synthesis recovery applies only to shell mode')
        recovery=check_synthesis_resume(job)
    if args.mode=='run':
        shell,parent=check_shell(job)
        amf_configuration(job)
        binary=Path(args.binary).resolve()
        for r in job['partitions']:
            if r.get('rm_rtl_sources'):
                if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',r.get('rm_top','')): raise ValueError('Explicit RM synthesis top required')
                if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',r.get('rm_clock_port','')): raise ValueError('Explicit RM clock port required')
                for f in r['rm_rtl_sources']+r.get('rm_ip_scripts',[]):
                    if not Path(f).is_file(): raise ValueError('Missing RM source '+f)
            elif not Path(r['rm_checkpoint']).is_file(): raise ValueError('Missing RM DCP')
        if not binary.is_file(): raise ValueError('Missing AMF binary')
    out.mkdir(parents=True,exist_ok=False); hashes=snapshot(out)
    t=time.monotonic()
    record=dict(schema='amf-dfx-multi-'+args.mode+'-v1',job=job,state='running',
                started=dt.datetime.now().astimezone().isoformat(),started_epoch=time.time(),stages=[],
                source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                script_hashes=hashes,dcp_storage='server-only',shell_mode='full Vivado shell; scoped AMF interface context, one active RP per worker')
    record['input_hashes']={f:sha(f) for k in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files') for f in job.get(k,[])}
    if recovery: record['synthesis_recovery']=recovery
    def update(stage):
        save(out/'manifest.json',record);save(out/'status.json',dict(state=record['state'],stage=stage,error=record.get('error')))
    update('starting')
    try:
        if args.mode=='shell':
            step=vivado(job,out,'dfx_multi_shell.tcl',settings(job,out));record['stages'].append(step)
            if step['exit_code']: raise RuntimeError('Multi-RP shell failed; see logs')
            record['acceptance']=acceptance(out,'base')
            if not record['acceptance']['implementation_verified']: raise RuntimeError('Parent implementation failed acceptance')
            names=['base_placed.dcp','base_routed.dcp','base_full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]
            if job.get('prepare_worker_shells', True):
                names += [r['id']+'_locked.dcp' for r in job['partitions']]
            record['outputs']={n:sha(out/n) for n in names}
        else:
            record['parent']=str(shell);record['parent_manifest_sha256']=sha(shell/'manifest.json')
            record['binary']=str(binary);record['binary_sha256']=sha(binary)
            record['rm_input_hashes']={r['id']:sha(r['rm_checkpoint']) for r in job['partitions'] if not r.get('rm_rtl_sources')}
            record['rm_source_hashes']={r['id']:{f:sha(f) for f in r.get('rm_rtl_sources',[])+r.get('rm_ip_scripts',[])} for r in job['partitions']}
            workers=out/'workers';workers.mkdir();record['workers']={}
            def worker(r):
                name=r['id'];cfg={k:job[k] for k in ('IncremenralCompilation','part','clock_name','clock_period_ns','amf_config','vivado','vivado_threads')}
                cfg['static_placement_audit']=job.get('static_placement_audit', 'full')
                begin=time.time(); frontend_step=None
                checkpoint=r.get('rm_checkpoint')
                if r.get('rm_rtl_sources'):
                    front=out/'frontend'/name
                    (front/'inputs').mkdir(parents=True);(front/'logs').mkdir()
                    for n in ('dfx_rm_synth.tcl','dfx_common.tcl'):shutil.copy2(out/'inputs'/n,front/'inputs'/n)
                    front_job=dict(job,rtl_sources=r['rm_rtl_sources'],ip_scripts=r.get('rm_ip_scripts',[]),
                                   rm_top=r['rm_top'],rm_clock_port=r['rm_clock_port'])
                    frontend_step=vivado(job,front,'dfx_rm_synth.tcl',settings(front_job,front))
                    if frontend_step['exit_code']:
                        return name,dict(exit_code=frontend_step['exit_code'],started_epoch=begin,ended_epoch=time.time(),frontend=frontend_step)
                    checkpoint=str(front/'rm.dcp')
                cfg.update(active_partition=r['active_partition'],pblock=r['pblock'],rm_checkpoint=checkpoint,
                           static_checkpoint=str(shell/(name+'_locked.dcp')),reference_checkpoint=str(shell/'base_routed.dcp'))
                if 'prepare_from_run' in r: cfg['prepare_from_run']=r['prepare_from_run']
                path=out/'inputs'/f'{name}.json';save(path,cfg)
                cmd=[sys.executable,str(out/'inputs/run_dfx.py'),'--config',str(path),'--output',str(workers/name),'--binary',str(binary)]
                with (out/'logs'/f'{name}.log').open('w') as f: rc=subprocess.run(cmd,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT).returncode
                return name,dict(exit_code=rc,started_epoch=begin,ended_epoch=time.time(),run=str(workers/name),frontend=frontend_step,rm_input_sha256=sha(checkpoint))
            update('parallel_amf_vivado')
            with ThreadPoolExecutor(max_workers=int(job['max_parallel'])) as pool:
                futures=[pool.submit(worker,r) for r in job['partitions']]
                for future in as_completed(futures):
                    name,result=future.result();record['workers'][name]=result;update('parallel_amf_vivado')
            if any(w['exit_code'] for w in record['workers'].values()): raise RuntimeError('Worker failed; successful outputs retained; see worker manifests')
            # Check child acceptance and actual module artifacts, not only process exit.
            for r in job['partitions']:
                m=json.loads((workers/r['id']/'manifest.json').read_text())
                if m['state']!='completed' or not m['acceptance']['implementation_verified']: raise RuntimeError('Worker not verified')
                record['workers'][r['id']]['rm_sha256']=sha(workers/r['id']/'rm_routed.dcp')
            assembly=out/'assembly';assembly.mkdir();(assembly/'inputs').mkdir();(assembly/'logs').mkdir()
            for n in ('dfx_multi_assemble.tcl','dfx_multi_common.tcl','dfx_common.tcl'):shutil.copy2(out/'inputs'/n,assembly/'inputs'/n)
            config=settings(job,assembly,dict(workers=str(workers),reference_checkpoint=str(shell/'base_routed.dcp')))
            update('assemble_routed_modules')
            step=vivado(job,assembly,'dfx_multi_assemble.tcl',config);record['stages'].append(step)
            if step['exit_code']: raise RuntimeError('Routed assembly failed; see assembly/logs')
            record['acceptance']=acceptance(assembly,'assembled')
            record['acceptance']['pr_verify_full_check']=(assembly/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
            if not record['acceptance']['implementation_verified'] or not record['acceptance']['pr_verify_full_check']: raise RuntimeError('Combined acceptance failed')
            names=['assembled_routed.dcp','full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]
            record['outputs']={str(assembly/n):sha(assembly/n) for n in names}
        record['state']='completed'
    except Exception as error:
        record['state']='failed';record['error']=str(error);raise
    finally:
        record['wall_seconds']=time.monotonic()-t;record['ended_epoch']=time.time();update('finished')
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=['shell','run']);p.add_argument('--config',required=True);p.add_argument('--output',required=True);p.add_argument('--binary')
    args=p.parse_args()
    if args.mode=='run' and not args.binary:p.error('--binary required')
    print(run(args))
