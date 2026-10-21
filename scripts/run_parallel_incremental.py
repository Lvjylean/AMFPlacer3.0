#!/usr/bin/env python3
"""Fresh A/B RTL synthesis through independent AMF workers, then one joint route.

Each complete module branch is one parallel task. No per-stage A/B barriers,
no independently routed RM assembly, no place_design fallback.
"""
import argparse
import copy
import datetime
import json
import os
from pathlib import Path
import shutil
import sys
import time
from run_dfx import save, sha, amf_configuration, source_provenance
import run_dfx_multi as flow
from common_static_contract import verify as verify_common_static
from parallel_runtime import command, parallel_barrier, require_memory, verify_hashes
from run_parallel_acceptance import run as verify_and_deliver
from optimized_rm_contract import verify_optimized
from run_auxiliary_reports import dispatch_after_completion

SCRIPTS=Path(__file__).resolve().parent


def preflight(config):
    job=flow.load(config)
    if (job['max_parallel']!=2 or job['vivado_threads']!=4 or float(job['clock_period_ns']) != float(job.get('experiment_clock_period_ns', 20))
            or job['backend_place_mode']!='route-only' or job['static_routing_containment']):
        raise ValueError('Unexpected frozen parallel flow conditions')
    if {p['id'] for p in job['partitions']} != {'a','b'}:
        raise ValueError('This validated scheduler requires A/B')
    common=Path(job['common_static_shell_run'])
    reference=Path(job['shell_run'])/'base_routed.dcp'
    contract=verify_common_static(job,common,reference)
    binary=Path(job['amf_binary'])
    helper=binary.parent/'partitionHyperGraph'
    if sha(helper)!=job['runtime_helper_sha256']:raise ValueError('Graph partition helper absent/changed')
    if sha(binary)!=job['amf_binary_sha256']: raise ValueError('AMF binary mismatch')
    provenance=source_provenance(binary)
    if job.get('native_static_cache'):
        from native_cache_contract import verify_cache
        verify_cache(job['native_static_cache'],dict(job,static_checkpoint=str(common/'common_static_locked.dcp')),amf_configuration(job))
    cfg=amf_configuration(job)
    from incremental_config import validate_incremental_binary
    validate_incremental_binary(job,binary)
    from paper_boundary_config import validate_paper_boundary_binary
    validate_paper_boundary_binary(cfg,binary)
    if str(cfg['jobs'])!='12': raise ValueError('AMF thread count changed')
    hashes={str(common/'common_static_locked.dcp'):sha(common/'common_static_locked.dcp'),
            str(common/'common_static_shell.json'):sha(common/'common_static_shell.json'),
            str(reference):sha(reference),str(binary):sha(binary),str(Path(job['amf_config'])):sha(job['amf_config'])}
    for key,value in cfg.items():
        if ('file' in key or key=='mergedSharedCellType2sharedCellType') and Path(value).is_file():
            hashes[value]=sha(value)
    def module_inputs(p):
        if not p.get('rm_rtl_sources') or not p.get('rm_top') or not p.get('rm_clock_port'):
            raise ValueError('Fresh module RTL synthesis inputs are required')
        if any(k in p for k in ('prepare_from_run','reuse_prepared_input_from_run')):
            raise ValueError('Fresh parallel measurement cannot reuse prepared workers')
        return {name:sha(name) for name in p['rm_rtl_sources']+p.get('rm_ip_scripts',[])}
    for module_hashes in parallel_barrier({p['id']:lambda p=p:module_inputs(p) for p in job['partitions']}).values():
        hashes.update(module_hashes)
    recoveries={}
    for part in job['partitions']:
        if not part.get('reuse_synthesis_from_branch'):
            if part.get('reuse_optimization_manifest'):raise ValueError('Orphan optimization recovery')
            continue
        parent=Path(part['reuse_synthesis_from_branch'])
        old=json.loads((parent/'manifest.json').read_text())
        whole=json.loads((parent.parents[1]/'manifest.json').read_text())
        if whole['state'] not in ('failed','completed') or old['synthesis']['exit_code']!=0:
            raise ValueError('Synthesis parent has not stopped with a successful synthesis')
        previous=next(p for p in whole['job']['partitions'] if p['id']==part['id'])
        for k in ('active_partition','pblock','rm_top','rm_clock_port','rm_rtl_sources','rm_ip_scripts'):
            if previous.get(k)!=part.get(k):raise ValueError('Synthesis recovery configuration mismatch: '+k)
        for k in ('part','clock_name','clock_period_ns','vivado_threads','vivado'):
            if whole['job'][k]!=job[k]:raise ValueError('Synthesis recovery tool/clock mismatch: '+k)
        for name in part['rm_rtl_sources']+part.get('rm_ip_scripts',[]):
            if hashes[name]!=whole['proof']['input_hashes'][name]:raise ValueError('Synthesis source changed')
            if sha(parent/'synthesis/sources'/Path(name).name)!=hashes[name]:raise ValueError('Frozen synthesis source changed')
        checkpoint=parent/'synthesis/rm.dcp'
        if sha(checkpoint)!=old['rm_synth_sha256']:raise ValueError('Synthesis DCP changed')
        optimized=verify_optimized(part['reuse_optimization_manifest'],job)
        if optimized['input_sha256']!=old['rm_synth_sha256'] or Path(optimized['input'])!=checkpoint:
            raise ValueError('Optimization has a different synthesized parent')
        recoveries[part['id']]=dict(parent_branch=str(parent),synthesis_checkpoint=str(checkpoint),
            synthesis=old['synthesis'],optimization=optimized,optimization_manifest=part['reuse_optimization_manifest'],
            inherited_failed_branch_seconds=old['seconds'])
        hashes[str(checkpoint)]=sha(checkpoint)
        hashes[str(parent/'manifest.json')]=sha(parent/'manifest.json')
        hashes[optimized['output']]=optimized['output_sha256']
        hashes[part['reuse_optimization_manifest']]=sha(part['reuse_optimization_manifest'])
    if recoveries and len(recoveries)!=2:raise ValueError('Recovery must explicitly identify both completed module producers')
    completed_modules={}
    for part in job['partitions']:
        if part.get('reuse_completed_module'):
            if part['id'] not in recoveries:raise ValueError('Completed module requires verified frontend provenance')
            from completed_module_recovery import verify_completed_module
            inherited=verify_completed_module(job,part,recoveries[part['id']],SCRIPTS)
            completed_modules[part['id']]=inherited
            hashes.update(inherited['hashes'])
    return job,dict(common_static=contract,source_provenance=provenance,input_hashes=hashes,
                    synthesis_reused=bool(recoveries),optimization_reused=bool(recoveries),recoveries=recoveries,
                    amf_reused=bool(completed_modules),completed_modules=completed_modules,static_shell_reused=True)


def run(config, output, workers_only=False):
    began=time.monotonic(); started_epoch=time.time()
    job,proof=preflight(config)
    out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    state=dict(schema='amf-parallel-incremental-v2',state='preflight',job=job,proof=proof,
               started_epoch=started_epoch,started_at=datetime.datetime.now().astimezone().isoformat(),
               runner_pid=os.getpid(),process_group=os.getpgrp(),workers={},
               endpoint='placed_modules_only' if workers_only else 'verified_full_and_partial_bitstreams',
               vivado_placement_executed=False,monitoring_restarted=False)
    save(out/'manifest.json',state)
    retained=None
    try:
        state['resource_preflight']=require_memory(60)
        frozen=out/'frozen_scripts';frozen.mkdir()
        for p in SCRIPTS.iterdir():
            if p.suffix in ('.py','.tcl'):shutil.copy2(p,frozen/p.name)
        state['script_hashes']={p.name:sha(p) for p in frozen.iterdir()}
        environment=dict(os.environ)
        environment['LD_LIBRARY_PATH']=job['amf_runtime_lib']+(':'+environment['LD_LIBRARY_PATH'] if environment.get('LD_LIBRARY_PATH') else '')
        if job.get('same_session_cache'):
            if workers_only:raise ValueError('Retained static session requires complete joint acceptance')
            from retained_joint_session import RetainedJointSession
            state['state']='same_session_cache_generation';save(out/'manifest.json',state)
            retained=RetainedJointSession(job,out,frozen,environment)
            job,certificate=retained.start_and_certify()
            state.update(job=job,retained_session=certificate)
            proof['local_cache_generation']=certificate
            proof['input_hashes'].update({job['native_static_cache']['path']:job['native_static_cache']['sha256'],
                job['native_static_cache']['contract']:job['native_static_cache']['contract_sha256']})
            state['resources_after_static_open']=require_memory(80)
        state['state']='parallel_module_branches';save(out/'manifest.json',state)

        def worker(part):
            k=part['id']; branch=out/'branches'/k;branch.mkdir(parents=True)
            begin=time.monotonic();started=time.time()
            rec=dict(state='synthesis',started_epoch=started,id=k)
            save(branch/'manifest.json',rec)
            try:
                if k in proof['completed_modules']:
                    inherited=proof['completed_modules'][k]
                    verify_hashes(inherited['hashes'])
                    rec.update(state='completed',amf_reused=True,coordinate_import_reused=True,
                               inherited_completed_module=inherited,inherited_frontend=proof['recoveries'][k],
                               synthesis_reused=True,optimization_reused=True)
                    for key in ('module','module_manifest_sha256','module_dcp_sha256','cells','binary_sha256'):
                        rec[key]=inherited[key]
                    return rec
                if k in proof['recoveries']:
                    inherited=proof['recoveries'][k]
                    checkpoint=Path(inherited['optimization']['output'])
                    optimization_manifest=Path(inherited['optimization_manifest'])
                    rec.update(inherited_frontend=inherited,synthesis_reused=True,optimization_reused=True,
                               rm_synth_sha256=inherited['optimization']['input_sha256'])
                else:
                    front=branch/'synthesis';(front/'inputs').mkdir(parents=True)
                    for p in frozen.iterdir():
                        if p.is_file() and p.suffix in ('.py','.tcl'):shutil.copy2(p,front/'inputs'/p.name)
                    sources=front/'sources';sources.mkdir(); mapping={}
                    for name in part['rm_rtl_sources']+part.get('rm_ip_scripts',[]):
                        source=Path(name);dest=sources/source.name
                        if dest.exists():raise ValueError('Source basename collision')
                        shutil.copy2(source,dest)
                        if sha(dest)!=proof['input_hashes'][name]:raise ValueError('Source changed while snapshotting')
                        mapping[name]=str(dest)
                    front_job=dict(job,rtl_sources=[mapping[x] for x in part['rm_rtl_sources']],
                                   ip_scripts=[mapping[x] for x in part.get('rm_ip_scripts',[])],
                                   rm_top=part['rm_top'],rm_clock_port=part['rm_clock_port'])
                    settings=flow.settings(front_job,front)
                    args=[job['vivado'],'-mode','batch','-source',str(front/'inputs/dfx_rm_synth.tcl'),'-tclargs',str(settings)]
                    rec['synthesis']=command(args,front/'exec',env=environment)
                    checkpoint=front/'rm.dcp'
                    if not checkpoint.stat().st_size:raise ValueError('Missing newly synthesized RM DCP')
                    optimized=branch/'optimization';(optimized/'inputs').mkdir(parents=True)
                    opt_settings=flow.settings(dict(job,rm_synth_checkpoint=str(checkpoint)),optimized)
                    opt_process=command([job['vivado'],'-mode','batch','-source',str(frozen/'dfx_rm_opt.tcl'),
                                         '-tclargs',str(opt_settings)],optimized/'exec',env=environment)
                    optimization_manifest=optimized/'manifest.json'
                    output=optimized/'rm_optimized.dcp'
                    save(optimization_manifest,dict(state='completed',process=opt_process,input=str(checkpoint),
                         input_sha256=sha(checkpoint),output=str(output),output_sha256=sha(output),
                         clock_period_ns=job['clock_period_ns'],vivado_threads=job['vivado_threads'],part=job['part']))
                    optimization=verify_optimized(optimization_manifest,job)
                    checkpoint=output
                    rec.update(rm_synth_sha256=optimization['input_sha256'],optimization=optimization)
                rec.update(state='prepare_amf_import',rm_optimized_sha256=sha(checkpoint))
                save(branch/'manifest.json',rec)
                cfg={key:job[key] for key in ('IncrementalCompilation','part','clock_name','clock_period_ns','amf_config','vivado','vivado_threads','static_placement_audit')}
                cfg.update(active_partition=part['active_partition'],pblock=part['pblock'],
                           static_checkpoint=str(Path(job['common_static_shell_run'])/'common_static_locked.dcp'),
                           reference_checkpoint=str(Path(job['shell_run'])/'base_routed.dcp'),
                           rm_checkpoint=str(checkpoint),rm_optimization_manifest=str(optimization_manifest),backend_place_mode='amf-import-only')
                worker_script='run_dfx.py'
                if job.get('native_static_cache'):
                    cfg.update(native_static_cache=job['native_static_cache'],module_id=part['id'],
                               native_dfx_edge_policy=job.get('native_dfx_edge_policy','off'),
                               native_pin_access_policy=job.get('native_pin_access_policy','off'),
                               native_local_pin_access_policy=job.get('native_local_pin_access_policy','off'))
                    if job.get('clock_handoff_mode') == 'inherit_static_primary':
                        cfg.update(clock_handoff_mode=job['clock_handoff_mode'],rm_clock_port=part['rm_clock_port'])
                    if part.get('reuse_native_export'):cfg['reuse_native_export']=part['reuse_native_export']
                    worker_script='run_native_module.py'
                cfgpath=branch/'worker.json';save(cfgpath,cfg)
                module=branch/'module'
                rec['worker_process']=command([sys.executable,str(frozen/worker_script),'--config',str(cfgpath),
                                               '--output',str(module),'--binary',job['amf_binary']],branch/'worker_exec',env=environment)
                m=json.loads((module/'manifest.json').read_text())
                if m['state']!='amf_placed' or m['amf_import_acceptance']['amf_loc_bel_exact'] is not True:
                    raise ValueError('Worker did not produce an exactly placed module')
                for name in ('imported.dcp','rm_amf_placed.dcp'):
                    if sha(module/name)!=m['outputs'][name]['sha256']:raise ValueError('Worker artifact hash mismatch')
                rec.update(state='completed',module=str(module),module_manifest_sha256=sha(module/'manifest.json'),
                           module_dcp_sha256=m['outputs']['rm_amf_placed.dcp']['sha256'],
                           cells=m['amf_import_acceptance']['cells'])
            except Exception as error:
                import traceback
                rec.update(state='failed',error=repr(error),error_traceback=traceback.format_exc());raise
            finally:
                rec.update(ended_epoch=time.time(),seconds=time.monotonic()-begin)
                save(branch/'manifest.json',rec)
            return rec

        workers=parallel_barrier({p['id']:lambda p=p:worker(p) for p in job['partitions']})
        state['workers']=workers
        state['worker_span_seconds']=max(w['ended_epoch'] for w in workers.values())-min(w['started_epoch'] for w in workers.values())
        state['worker_overlap_seconds']=max(0,min(w['ended_epoch'] for w in workers.values())-max(w['started_epoch'] for w in workers.values()))
        verify_hashes(proof['input_hashes'])
        if workers_only:
            state['state']='placed_modules_completed_not_joint_accepted'
            return out

        state['state']='joint_route';save(out/'manifest.json',state)
        joint=out/'joint'
        if retained is None:
            (joint/'inputs').mkdir(parents=True)
            for p in frozen.glob('*.tcl'):shutil.copy2(p,joint/'inputs'/p.name)
        metadata=joint/'inputs/common_static'
        if retained is None:metadata.mkdir()
        common=Path(job['common_static_shell_run'])
        for p in job['partitions']:
            k=p['id']
            if retained is None:shutil.copy2(common/(k+'_common_static_boundary.tcldata'),metadata/(k+'_common_static_boundary.tcldata'))
        extra=dict(common_static_checkpoint=str(common/'common_static_locked.dcp'),common_static_metadata=str(metadata),
                   reference_checkpoint=str(Path(job['shell_run'])/'base_routed.dcp'))
        native_job=copy.deepcopy(job)
        native_job['joint_backend_source_workers']={k:w['module'] for k,w in workers.items()}
        for k,w in workers.items():
            dest=joint/'inputs'/('worker_'+k);dest.mkdir()
            names=['placement.tsv','blocked_sites.txt']
            if job.get('native_local_pin_access_policy','off')!='off':names.append('lut_pin_assignments.tsv')
            for name in names:shutil.copy2(Path(w['module'])/name,dest/name)
            extra.update({ 'worker_'+k:str(dest),'rm_'+k+'_checkpoint':str(Path(w['module'])/'rm_amf_placed.dcp'),
                          'expected_cells_'+k:w['cells']})
        settings=retained.settings if retained is not None else flow.settings(native_job,joint,extra)
        jm=dict(state='running',job=native_job,started_epoch=retained.started_epoch if retained else time.time(),source_workers=workers,outputs={})
        save(joint/'manifest.json',jm)
        try:
            if retained is not None:
                jm['process']=retained.finish(workers)
            else:
                jm['process']=command([job['vivado'],'-mode','batch','-source',str(joint/'inputs/dfx_joint_backend.tcl'),
                                       '-tclargs',str(settings)],joint/'exec',env=environment)
            for name in ('joint_amf_placed.dcp','joint_routed.dcp'):
                p=joint/name;jm['outputs'][name]=dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size)
            times=[line.split('\t') for line in (joint/'stage_times.tsv').read_text().splitlines()]
            if len([r for r in times if r[0]=='vivado_route' and r[2]=='0'])!=1 or any(r[0]=='vivado_place' for r in times):
                raise ValueError('Joint route-only execution contract violated')
            jm['state']='routed_awaiting_verification'
        except Exception as error:
            jm.update(state='failed',error=repr(error));raise
        finally:save(joint/'manifest.json',jm)
        state.update(state='parallel_final_acceptance',routed_dcp_ready_epoch=time.time());save(out/'manifest.json',state)
        verify_and_deliver(joint,out/'acceptance')
        verify_hashes(proof['input_hashes'])
        state.update(state='completed',acceptance_manifest_sha256=sha(out/'acceptance/manifest.json'))
    except Exception as error:
        if retained is not None:retained.abort(repr(error))
        state.update(state='failed',error=repr(error));raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-began)
        save(out/'manifest.json',state)
    if state['state']=='completed':
        dispatch_after_completion(out/'joint',out/'background_reports',out/'manifest.json')
    return out


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',required=True);parser.add_argument('--output',required=True)
    parser.add_argument('--workers-only',action='store_true',help='Validate fresh parallel module producers only; never claim joint acceptance')
    a=parser.parse_args();print(run(a.config,a.output,a.workers_only))
