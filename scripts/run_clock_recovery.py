#!/usr/bin/env python3
"""Recover OOC clock ownership, reuse exact AMF placements, route and verify."""
import argparse, copy, datetime, json, os, shutil, sys, time
from pathlib import Path
import run_dfx_multi as flow
from run_dfx import save, sha
from parallel_runtime import command, parallel_barrier, require_memory, verify_hashes
from common_static_contract import verify as verify_common_static
from run_parallel_acceptance import run as verify_and_deliver
from run_auxiliary_reports import dispatch_after_completion
from timing_coverage import counts, verify as verify_coverage

SCRIPTS = Path(__file__).resolve().parent

def stages(path):
    return {r[0]:float(r[1]) for line in path.read_text().splitlines()
            if len(r:=line.split('\t'))==3 and r[2]=='0'}

def run(parent, full, probe, out):
    began=time.monotonic(); started=time.time()
    parent, full, probe, out=map(lambda p:Path(p).resolve(),(parent,full,probe,out))
    out.mkdir(parents=True,exist_ok=False)
    state=dict(schema='amf-ooc-clock-recovery-v1',state='preflight',started_epoch=started,
               runner_pid=os.getpid(),process_group=os.getpgrp(),parent_incremental=str(parent),
               full_reference=str(full),probe=str(probe),monitoring_restarted=False,
               amf_repeated=False,synthesis_repeated=False,placement_executed=False)
    def commit():save(out/'manifest.json',state)
    commit()
    try:
        old=json.loads((parent/'manifest.json').read_text())
        fm=json.loads((full/'manifest.json').read_text())
        if old['state']!='completed' or fm['state']!='completed':raise ValueError('Parents are not stopped')
        job=copy.deepcopy(old['job'])
        if (job['vivado_threads']!=4 or job['clock_period_ns']!=12 or
                job['backend_place_mode']!='route-only' or job['static_routing_containment']):
            raise ValueError('Recovery conditions changed')
        baseline=full/'base_timing.rpt'
        limits=counts(baseline.read_text())
        if limits!={'no_clock':1,'unconstrained_internal_endpoints':1}:
            raise ValueError('Unexpected accepted reference coverage; review before proceeding')
        job.update(clock_contract_enabled=True,clock_root_pin='jtag/TCK',clock_rm_port='ap_clk',
                   clock_handoff_mode='inherit_static_primary',
                   clock_max_no_clock=limits['no_clock'],
                   clock_max_unconstrained_internal_endpoints=limits['unconstrained_internal_endpoints'],
                   clock_timing_baseline=dict(path=str(baseline),sha256=sha(baseline)))
        common=Path(job['common_static_shell_run'])
        reference=Path(job['shell_run'])/'base_routed.dcp'
        contract=verify_common_static(job,common,reference)
        expected_ref=json.loads((Path(job['shell_run'])/'manifest.json').read_text())['outputs']['base_routed.dcp']
        if sha(reference)!=expected_ref:raise ValueError('Static routed reference changed')
        if sha(job['amf_binary'])!=job['amf_binary_sha256']:raise ValueError('AMF producer binary changed')
        if fm['binary_sha256']!=job['amf_binary_sha256']:
            raise ValueError('Full V1 and incremental AMF binary identities differ')
        workers=old['workers']
        immutable={str(p):sha(p) for p in [parent/'manifest.json',full/'manifest.json',baseline,
                     common/'common_static_locked.dcp',common/'common_static_shell.json',reference]}
        for k,w in workers.items():
            module=Path(w['module']);m=json.loads((module/'manifest.json').read_text())
            if m['state']!='amf_placed' or not m['amf_import_acceptance']['amf_loc_bel_exact']:
                raise ValueError('AMF parent is not exactly placed')
            if sha(module/'manifest.json')!=w['module_manifest_sha256']:
                raise ValueError('Worker manifest changed')
            if m['binary_sha256']!=job['amf_binary_sha256']:
                raise ValueError('Worker AMF binary identity changed')
            if sha(module/'rm_amf_placed.dcp')!=w['module_dcp_sha256']:
                raise ValueError('AMF placed module changed')
            for name in ['manifest.json','rm_amf_placed.dcp','placement.tsv','blocked_sites.txt']:
                immutable[str(module/name)]=sha(module/name)
            immutable[str(parent/'branches'/k/'synthesis/clock.xdc')]=sha(parent/'branches'/k/'synthesis/clock.xdc')
        state.update(job=job,immutable_inputs=immutable,static_shell_contract=contract,
                     inherited_workers=workers,resource_preflight=require_memory(60))
        frozen=out/'frozen_scripts';frozen.mkdir()
        for src in SCRIPTS.iterdir():
            if src.suffix in ('.py','.tcl'):shutil.copy2(src,frozen/src.name)
        state['script_hashes']={p.name:sha(p) for p in frozen.iterdir()}
        state['state']='retagging_ooc_clocks';commit()

        def retag(part):
            k=part['id']; module=Path(workers[k]['module'])
            dest=out/'modules'/k;(dest/'inputs').mkdir(parents=True)
            for src in frozen.glob('*.tcl'):shutil.copy2(src,dest/'inputs'/src.name)
            extra=dict(source_module=str(module/'rm_amf_placed.dcp'),active_partition=part['active_partition'],
                       placement_table=str(module/'placement.tsv'),expected_cells=workers[k]['cells'],
                       rm_clock_port=part['rm_clock_port'],ooc_clock_xdc=str(parent/'branches'/k/'synthesis/clock.xdc'))
            config=flow.settings(job,dest,extra)
            rec=dict(state='running',source_module=str(module),source_manifest_sha256=sha(module/'manifest.json'),
                     source_checkpoint_sha256=sha(module/'rm_amf_placed.dcp'),started_epoch=time.time())
            save(dest/'manifest.json',rec)
            try:
                rec['process']=command([job['vivado'],'-mode','batch','-source',str(dest/'inputs/dfx_retag_ooc_clock.tcl'),
                                        '-tclargs',str(config)],dest/'exec',timeout=1800)
                for name in ('placement.tsv','blocked_sites.txt'):shutil.copy2(module/name,dest/name)
                checkpoint=dest/'rm_amf_placed.dcp'
                rec.update(state='completed',checkpoint=str(checkpoint),sha256=sha(checkpoint),
                           cells=workers[k]['cells'],amf_loc_bel_exact=True)
            except Exception as e:
                rec.update(state='failed',error=repr(e));raise
            finally:
                rec['ended_epoch']=time.time();save(dest/'manifest.json',rec)
            return rec
        recovered=parallel_barrier({p['id']:lambda p=p:retag(p) for p in job['partitions']})
        state['recovered_modules']=recovered
        verify_hashes(immutable)
        # Probe and retag operate independently. Do not route until the restored
        # historical clock has demonstrated complete coverage in a real design.
        state['state']='awaiting_clock_probe';commit()
        deadline=time.monotonic()+1800
        while True:
            pm=json.loads((probe/'manifest.json').read_text())
            if pm['state']!='running':break
            if time.monotonic()>deadline:raise RuntimeError('Clock diagnostic did not finish')
            time.sleep(5)
        if pm['state']!='completed' or not pm['source_unchanged']:raise ValueError('Clock diagnostic failed')
        state['historical_route_clock_probe']=dict(manifest=pm,
            timing_coverage=verify_coverage(probe/'corrected_timing.rpt',job['clock_timing_baseline']))

        state['state']='joint_route';commit()
        joint=out/'joint';(joint/'inputs').mkdir(parents=True)
        for src in frozen.glob('*.tcl'):shutil.copy2(src,joint/'inputs'/src.name)
        metadata=joint/'inputs/common_static';metadata.mkdir()
        extra=dict(common_static_checkpoint=str(common/'common_static_locked.dcp'),common_static_metadata=str(metadata),
                   reference_checkpoint=str(reference))
        for part in job['partitions']:
            k=part['id'];shutil.copy2(common/(k+'_common_static_boundary.tcldata'),metadata/(k+'_common_static_boundary.tcldata'))
            dest=joint/'inputs'/('worker_'+k);dest.mkdir()
            for name in ('placement.tsv','blocked_sites.txt'):shutil.copy2(out/'modules'/k/name,dest/name)
            extra.update({'worker_'+k:str(dest),'rm_'+k+'_checkpoint':recovered[k]['checkpoint'],'expected_cells_'+k:workers[k]['cells']})
        config=flow.settings(job,joint,extra)
        jm=dict(state='running',job=job,started_epoch=time.time(),source_workers=recovered,outputs={})
        save(joint/'manifest.json',jm)
        try:
            jm['process']=command([job['vivado'],'-mode','batch','-source',str(joint/'inputs/dfx_joint_backend.tcl'),
                                   '-tclargs',str(config)],joint/'exec',timeout=14400)
            for name in ('joint_amf_placed.dcp','joint_routed.dcp'):
                f=joint/name;jm['outputs'][name]=dict(path=str(f),sha256=sha(f),bytes=f.stat().st_size)
            timing_rows=(joint/'stage_times.tsv').read_text().splitlines()
            if sum(x.startswith('vivado_route\t') for x in timing_rows)!=1 or any('vivado_place' in x for x in timing_rows):
                raise ValueError('Joint route-only execution contract violated')
            jm['state']='routed_awaiting_verification'
        except Exception as e:
            jm.update(state='failed',error=repr(e));raise
        finally:save(joint/'manifest.json',jm)
        state['state']='parallel_final_acceptance';commit()
        verify_and_deliver(joint,out/'acceptance')
        verify_hashes(immutable)
        acceptance=json.loads((out/'acceptance/manifest.json').read_text())
        if not acceptance['acceptance']['timing_coverage']['passed']:raise ValueError('Missing final clock coverage')
        state.update(state='completed',accepted=True,acceptance_manifest_sha256=sha(out/'acceptance/manifest.json'),
                     acceptance=acceptance['acceptance'],pr_verify_full_check=acceptance['pr_verify_full_check'],
                     outputs=acceptance['outputs'],source_hashes_unchanged=True)
        old_comparison=parent.parents[1]/'comparisons/getrf-12ns-right3-region-legalizer-38/comparison.json'
        prior=json.loads(old_comparison.read_text())
        jt=stages(joint/'stage_times.tsv')
        joint_seconds=jm['process']['seconds']
        branch_span=old['worker_span_seconds']
        accepted_seconds=acceptance['wall_seconds']
        comparison=dict(schema='clock-corrected-recovery-comparison-v1',accepted=True,
            full_reference=str(full),recovery_run=str(out),same_full_v1_incremental_binary=True,
            binary_sha256=job['amf_binary_sha256'],static_cache_producer_same_binary=False,
            static_cache_producer_binary_sha256=prior['static_cache_producer_binary_sha256'],
            full_acceptance=fm['acceptance'],incremental_acceptance=acceptance['acceptance'],
            pr_verify_full_check=acceptance['pr_verify_full_check'],
            full_route_seconds=prior['full_route_seconds'],corrected_joint_route_seconds=jt['vivado_route'],
            route_ratio=jt['vivado_route']/prior['full_route_seconds'],
            route_70pct_pass=jt['vivado_route']<=0.7*prior['full_route_seconds'],
            inherited_branch_span_seconds=branch_span,inherited_max_branch_seconds=max(w['seconds'] for w in workers.values()),
            new_joint_process_seconds=joint_seconds,new_acceptance_seconds=accepted_seconds,
            reconstructed_incremental_seconds=branch_span+joint_seconds+accepted_seconds,
            reconstructed_full_seconds=prior['full_v1_reconstructed_main_seconds'],
            full_vivado_placement_seconds=prior['full_compatibility_placement'],
            reconstructed_full_minus_vivado_placement_seconds=prior['full_v1_reconstructed_minus_vivado_place'],
            timing_note='Reconstructed stage sums combine measured inherited frontend and new corrected backend; not one observed cold-run wall. Retag/probe/recovery overhead and old invalid run are separate.',
            old_invalid_incremental_wall_seconds=old['wall_seconds'],old_invalid_route_seconds=prior['incremental_joint_route_seconds'],
            historical_failed_full_amf_seconds=prior['historical_failed_v1_amf_seconds'],
            clock_probe=state['historical_route_clock_probe'],joint_stages=jt,outputs=acceptance['outputs'],
            dcp=str(joint/'joint_routed.dcp'))
        save(out/'comparison.json',comparison)
    except Exception as e:
        state.update(state='failed',accepted=False,error=repr(e));raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-began);commit()
    dispatch_after_completion(out/'joint',out/'background_reports',out/'manifest.json')
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--parent',required=True);p.add_argument('--full',required=True)
    p.add_argument('--probe',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();print(run(a.parent,a.full,a.probe,a.output))
