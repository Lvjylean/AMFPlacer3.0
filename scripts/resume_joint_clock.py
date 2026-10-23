#!/usr/bin/env python3
"""Immutable joint-DCP recovery with unchanged placement and full acceptance."""
import argparse,copy,json,os,shutil,time
from pathlib import Path
from run_dfx import save,sha
import run_dfx_multi as flow
from parallel_runtime import command,require_memory,verify_hashes
from run_parallel_acceptance import run as verify_and_deliver
from run_auxiliary_reports import dispatch_after_completion
SCRIPTS=Path(__file__).resolve().parent

def run(config, output):
    cfg=json.loads(Path(config).read_text()); parent=Path(cfg['parent'])
    old=json.loads((parent/'manifest.json').read_text())
    source=parent/'joint'; jm0=json.loads((source/'manifest.json').read_text())
    if old['state']!='failed' or jm0['state']!='failed':raise ValueError('Recovery parent is not stopped/failed')
    verify_hashes(cfg['immutable_inputs'])
    rows=[s.split('\t') for s in (source/'stage_times.tsv').read_text().splitlines()]
    if not any(x[0]=='write_joint_imported' and x[2]=='0' for x in rows):raise ValueError('Missing successful imported DCP')
    if any(x[0] in ('update_clock_routing','vivado_route') for x in rows):raise ValueError('Recovery requires checkpoint before clock update and route')
    job=copy.deepcopy(jm0['job']); flow.validate(job)
    if job.get('clock_guidance_mode')!='update-preserve':raise ValueError('Clock update not authorized by parent')
    workers=old['workers']
    for k,w in workers.items():
        m=Path(w['module']); mm=json.loads((m/'manifest.json').read_text())
        if (mm['state']!='amf_placed' or not mm['amf_import_acceptance']['amf_loc_bel_exact'] or
            sha(m/'manifest.json')!=w['module_manifest_sha256'] or sha(m/'rm_amf_placed.dcp')!=w['module_dcp_sha256']):
            raise ValueError('Worker certificate changed: '+k)
    resource=require_memory(80)
    out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    started=time.monotonic()
    state=dict(schema='amf-joint-clock-recovery-v1',state='preflight',started_epoch=time.time(),
        runner_pid=os.getpid(),process_group=os.getpgrp(),parent=str(parent),job=job,
        workers=workers,immutable_inputs=cfg['immutable_inputs'],resources=resource,
        synthesis_repeated=False,amf_repeated=False,cache_repeated=False,placement_executed=False,
        not_fresh_parallel_measurement=True,monitoring_restarted=False)
    def commit():save(out/'manifest.json',state)
    commit()
    try:
        joint=out/'joint';(joint/'inputs').mkdir(parents=True)
        for src in SCRIPTS.iterdir():
            if src.is_file() and src.suffix in ('.py','.tcl'):shutil.copy2(src,joint/'inputs'/src.name)
        for name in ['common_static']+['worker_'+k for k in workers]:
            shutil.copytree(source/'inputs'/name,joint/'inputs'/name)
        for name in ['imported_rp_route_footprints.tcldata']+(['static_snapshot.tcldata'] if job.get('static_placement_audit')=='full' else []):
            shutil.copy2(source/name,joint/name)
        extra=dict(recovery_checkpoint=str(source/'joint_amf_placed.dcp'),
            common_static_metadata=str(joint/'inputs/common_static'),
            reference_checkpoint=str(Path(job['shell_run'])/'base_routed.dcp'))
        for k,w in workers.items():extra.update({'worker_'+k:str(joint/'inputs'/('worker_'+k)),'expected_cells_'+k:w['cells']})
        settings=flow.settings(job,joint,extra)
        cp=source/'joint_amf_placed.dcp'
        jm=dict(state='running',job=job,started_epoch=time.time(),source_workers=workers,
            parent_joint=str(source),input_checkpoint_sha256=sha(cp),
            outputs={'joint_amf_placed.dcp':dict(path=str(cp),sha256=sha(cp),bytes=cp.stat().st_size,inherited=True)})
        save(joint/'manifest.json',jm)
        state.update(state='joint_route',script_hashes={p.name:sha(p) for p in (joint/'inputs').iterdir() if p.is_file()});commit()
        try:
            jm['process']=command([job['vivado'],'-mode','batch','-source',str(joint/'inputs/dfx_resume_joint_clock.tcl'),
                '-tclargs',str(settings)],joint/'exec')
            times=[s.split('\t') for s in (joint/'stage_times.tsv').read_text().splitlines()]
            for stage in ('update_clock_routing','clock_guidance_placement_verify','clock_guidance_fixed_verify','vivado_route'):
                if sum(x[0]==stage and x[2]=='0' for x in times)!=1:raise ValueError('Incomplete/duplicate stage: '+stage)
            if any(x[0]=='vivado_place' for x in times):raise ValueError('Unexpected placer')
            cp=joint/'joint_routed.dcp';jm['outputs']['joint_routed.dcp']=dict(path=str(cp),sha256=sha(cp),bytes=cp.stat().st_size)
            jm.update(state='routed_awaiting_verification')
        except Exception as error:
            jm.update(state='failed',error=repr(error));raise
        finally:save(joint/'manifest.json',jm)
        verify_hashes(cfg['immutable_inputs'])
        state.update(state='parallel_final_acceptance',routed_dcp_ready_epoch=time.time());commit()
        verify_and_deliver(joint,out/'acceptance')
        verify_hashes(cfg['immutable_inputs'])
        state.update(state='completed',acceptance_manifest_sha256=sha(out/'acceptance/manifest.json'),source_hashes_unchanged=True)
    except Exception as error:
        state.update(state='failed',error=repr(error));raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-started);commit()
    dispatch_after_completion(out/'joint',out/'background_reports',out/'manifest.json')
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();print(run(a.config,a.output))
