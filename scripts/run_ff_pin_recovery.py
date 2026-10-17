#!/usr/bin/env python3
"""One-shot, opt-in FF access recovery of a previously imported joint design."""
from pathlib import Path
import argparse, json, os, shutil, time
from run_dfx import save, sha
import run_dfx_multi as flow
from parallel_runtime import command, require_memory, verify_hashes
from run_parallel_acceptance import run as accept


def run(evidence, output):
    e=Path(evidence).resolve(); out=Path(output).resolve()
    if out.exists(): raise ValueError('Recovery output already exists; no implicit retry')
    old=e/'inputs/original'
    original=json.loads((old/'joint/manifest.json').read_text())
    if original['state']!='failed': raise ValueError('Expected preserved failed source')
    prep=json.loads((e/'access-preparation.json').read_text())
    native=json.loads((e/'native-repair-results.json').read_text())
    checkpoint=Path(prep['source_checkpoint'])
    if sha(checkpoint)!=prep['source_checkpoint_sha256']: raise ValueError('Changed imported checkpoint')
    if prep['new_extraction']: raise ValueError('Unexpected new extraction')
    preprocessing=prep['data_preparation_seconds']+sum(v['process_seconds'] for v in native.values())
    if preprocessing>180 or any(v['exit_code'] for v in native.values()): raise ValueError('Repair preprocessing failed/budget exceeded')
    job=json.loads(json.dumps(original['job']))
    job['vivado']='/Projects/Xilinx26/2026.1/Vivado/bin/vivado'
    job['shell_run']=str(e/'inputs/v0_reference')
    job['clock_timing_baseline']['path']=str(e/'inputs/base_timing.rpt')
    job['recovery_parent_host']='coldsun'
    job['recovery_reused_completed_module_branches']=True
    job['recovery_fresh_synthesis_executed']=False
    job['recovery_global_amf_executed']=False
    flow.validate(job)
    if sha(job['clock_timing_baseline']['path'])!=job['clock_timing_baseline']['sha256']: raise ValueError('Timing baseline changed')
    parent=json.loads((Path(job['shell_run'])/'manifest.json').read_text())
    h=parent['outputs']['base_routed.dcp']; h=h['sha256'] if isinstance(h,dict) else h
    if sha(Path(job['shell_run'])/'base_routed.dcp')!=h: raise ValueError('V0 reference changed')
    resources=require_memory(80)
    out.mkdir(parents=True)
    started=time.monotonic()
    state=dict(schema='amf-native-ff-pin-recovery-v1',state='preflight',started_epoch=time.time(),
               pid=os.getpid(),host=os.uname().nodename,job=job,resources=resources,
               source_manifest_sha256=sha(old/'joint/manifest.json'),
               source_checkpoint=str(checkpoint),source_checkpoint_sha256=prep['source_checkpoint_sha256'],
               repair_preprocessing_seconds=preprocessing,repair_budget_seconds=180,
               full_rerun=False,synthesis_rerun=False,global_amf_rerun=False,place_design_executed=False,
               outputs={})
    save(out/'manifest.json',state)
    try:
        inputs=out/'inputs'; shutil.copytree(old/'joint/inputs',inputs)
        # Existing acceptance code and conditions are reused. Only this recovery
        # Tcl replaces import with a checked local FF BEL repair.
        source=Path(__file__).parent
        shutil.copy2(source/'dfx_ff_pin_recovery.tcl',inputs/'dfx_ff_pin_recovery.tcl')
        immutable={str(checkpoint):prep['source_checkpoint_sha256']}
        extra=dict(recovery_checkpoint=str(checkpoint),repair_preprocessing_seconds=preprocessing,
                   repair_budget_seconds=180,common_static_metadata=str(inputs/'common_static'),
                   imported_footprints=str(old/'joint/imported_rp_route_footprints.tcldata'))
        changes=[]
        for part in job['partitions']:
            k=part['id']; new=e/('native_'+k)
            sourceplacement=old/'branches'/k/'module/placement.tsv'
            def placement(p):
                rows=[x.split('\t') for x in p.read_text().splitlines() if x]
                d=dict(rows)
                if len(d)!=len(rows):raise ValueError('Duplicate placement')
                return d
            before=placement(sourceplacement); after=placement(new/'placement.tsv')
            if before.keys()!=after.keys():raise ValueError('Cell coverage changed')
            delta=[x.split('\t') for x in (new/'placement_delta.tsv').read_text().splitlines()[1:] if x]
            expected={n:(before[n],after[n]) for n in before if before[n]!=after[n]}
            if expected!={n:(a,b) for n,a,b in delta}:raise ValueError('Delta mismatch')
            for n,a,b in delta:
                if not n.startswith(part['active_partition']+'/') or a.split('/')[0]!=b.split('/')[0]:raise ValueError('Scope/site changed')
                changes.append(dict(module=k,cell=n,source=a,target=b))
            shutil.copy2(new/'placement.tsv',inputs/('worker_'+k)/'placement.tsv')
            shutil.copy2(new/'placement_delta.tsv',inputs/('worker_'+k)/'placement_delta.tsv')
            extra.update({'worker_'+k:str(inputs/('worker_'+k)), 'expected_cells_'+k:len(after),
                          'delta_'+k:str(inputs/('worker_'+k)/'placement_delta.tsv')})
            cert=e/'inputs'/(k+'_ff_access.tsv')
            if sha(cert)!=prep['certificates'][k]['sha256']:raise ValueError('Access sidecar changed')
            for f in (sourceplacement,new/'placement.tsv',new/'placement_delta.tsv',cert):immutable[str(f)]=sha(f)
        state['changes']=changes; state['immutable_inputs']=immutable
        config=flow.settings(job,out,extra)
        state['script_hashes']={p.name:sha(p) for p in inputs.glob('*.tcl')}
        save(out/'manifest.json',state)
        verify_hashes(immutable)
        state['state']='repair_and_joint_route';save(out/'manifest.json',state)
        state['backend']=command(['/usr/bin/time','-v','-o',str(out/'process-resource.txt'),job['vivado'],
                                  '-mode','batch','-source',str(inputs/'dfx_ff_pin_recovery.tcl'),'-tclargs',str(config)],
                                 out/'exec',timeout=7200)
        verify_hashes(immutable)
        routed=out/'joint_routed.dcp'
        state['outputs']['joint_routed.dcp']=dict(sha256=sha(routed),bytes=routed.stat().st_size)
        state['state']='routed_awaiting_verification';save(out/'manifest.json',state)
        state['acceptance_directory']=str(accept(out,out/'acceptance'))
        acceptance=json.loads((out/'acceptance/manifest.json').read_text())
        if acceptance['state']!='completed' or not acceptance['pr_verify_full_check']:raise ValueError('Incomplete acceptance')
        state['acceptance']=acceptance['acceptance'];state['bitstreams']=acceptance['outputs']
        state['state']='completed_awaiting_evidence_review'
    except Exception as ex:
        state.update(state='failed',error=repr(ex))
        raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-started)
        save(out/'manifest.json',state)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--evidence',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();run(a.evidence,a.output)
