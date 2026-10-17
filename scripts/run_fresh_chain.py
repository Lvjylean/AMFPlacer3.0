"""Adopt running fresh V0, parallel fresh V1, then incremental acceptance."""
import argparse, datetime, hashlib, json, os, subprocess, sys, time, traceback
from pathlib import Path
from run_dfx import save, sha
from parallel_runtime import require_memory, verify_hashes

D = Path(__file__).resolve().parent.parent
S = D/'scripts'

def table(path):
    return [dict(stage=r[0],seconds=float(r[1]),exit_code=int(r[2]))
            for line in path.read_text().splitlines() if (r:=line.split('\t'))]

def background_full(source, parent, output):
    """Read-only descriptive reporting, excluded from all main-flow timers."""
    import run_dfx_multi as flow
    out=Path(output);out.mkdir(exist_ok=False)
    (out/'inputs').mkdir()
    m=json.loads(Path(parent).read_text());job=m['job']
    settings=flow.settings(job,out,dict(routed_checkpoint=str(Path(source)/'base_routed.dcp')))
    record=dict(state='running',excluded_from_main_flow=True,source=str(source),
                parent_manifest=str(parent),parent_sha256=sha(parent),started_epoch=time.time())
    save(out/'manifest.json',record)
    with (out/'process.log').open('w') as log:
        p=subprocess.Popen([job['vivado'],'-mode','batch','-source',str(S/'dfx_auxiliary_reports.tcl'),
                            '-tclargs',str(settings)],cwd=out,stdout=log,stderr=subprocess.STDOUT)
        record['child_pid']=p.pid;save(out/'manifest.json',record);rc=p.wait()
    record.update(state='completed' if rc==0 else 'failed',exit_code=rc,ended_epoch=time.time())
    record['wall_seconds']=record['ended_epoch']-record['started_epoch']
    save(out/'manifest.json',record)

def run(output):
    out=Path(output);out.mkdir(exist_ok=False)
    plan=json.loads((D/'manifest.json').read_text())
    verify_hashes({str(D/k):v for k,v in plan['files'].items()})
    state=dict(schema='getrf-fresh-chain-v1',state='starting',runner_pid=os.getpid(),
               process_group=os.getpgrp(),started_epoch=time.time(),deployment=str(D),
               deployment_manifest_sha256=sha(D/'manifest.json'),runs=plan['runs'],steps=[],
               monitoring_automation='PAUSED',dcp_storage='server-only')
    save(out/'manifest.json',state)
    def step(name, argv):
        require_memory(80)
        verify_hashes({str(D/k):v for k,v in plan['files'].items()})
        job=json.loads((D/'configs/v0-full.json').read_text())
        env=dict(os.environ,LD_LIBRARY_PATH=job['amf_runtime_lib']+':'+os.environ.get('LD_LIBRARY_PATH',''))
        rec=dict(name=name,command=argv,started_epoch=time.time(),state='running')
        state['state']=name;state['steps'].append(rec)
        with (out/(name+'.log')).open('w') as log:
            p=subprocess.Popen(argv,cwd=out,stdout=log,stderr=subprocess.STDOUT,env=env)
            rec['pid']=p.pid;save(out/'manifest.json',state);rc=p.wait()
        rec.update(exit_code=rc,ended_epoch=time.time(),state='completed' if rc==0 else 'failed')
        rec['wall_seconds']=rec['ended_epoch']-rec['started_epoch'];save(out/'manifest.json',state)
        if rc: raise RuntimeError(name+' failed; all evidence preserved; no automatic retries')
        return json.loads((Path(plan['runs'][name])/'manifest.json').read_text())
    try:
        from repaired_full_pair import run_full_pair
        fulls=run_full_pair(D,out,plan,state)
        v0=Path(plan['runs']['v0-full']);v1=Path(plan['runs']['v1-full'])
        native=dict(path=str(v0/'native_cache/sealed.bin'),sha256=sha(v0/'native_cache/sealed.bin'),
                    contract=str(v0/'native_cache/contract.json'),contract_sha256=sha(v0/'native_cache/contract.json'))
        incremental=json.loads((D/'configs/v1-incremental-template.json').read_text())
        incremental['native_static_cache']=native
        # The only reusable implementation inputs are produced by this fresh V0.
        incremental['fresh_parent_proof']=dict(full_v0_manifest_sha256=sha(v0/'manifest.json'),
            full_v1_manifest_sha256=sha(v1/'manifest.json'),v1_synthesis_sha256=sha(v1/'base_synth.dcp'))
        config=out/'v1-incremental.json';save(config,incremental)
        inc=step('v1-incremental',[sys.executable,str(S/'run_parallel_incremental.py'),
                  '--config',str(config),'--output',plan['runs']['v1-incremental']])
        if inc['state']!='completed':raise RuntimeError('Incremental acceptance incomplete')
        incout=Path(plan['runs']['v1-incremental'])
        full_times=table(v1/'stage_times.tsv');joint_times=table(incout/'joint/stage_times.tsv')
        full_route=sum(r['seconds'] for r in full_times if r['stage']=='vivado_route')
        joint_route=sum(r['seconds'] for r in joint_times if r['stage']=='vivado_route')
        full_place=sum(r['seconds'] for r in full_times if r['stage']=='vivado_place')
        acceptance=json.loads((incout/'acceptance/manifest.json').read_text())
        comparison=dict(schema='getrf-fresh-comparison-v1',clock_period_ns=12,regions=plan['geometry'],
            full_binary_sha256=fulls['v1-full']['binary_sha256'],incremental_binary_sha256=incremental['amf_binary_sha256'],
            same_binary=fulls['v1-full']['binary_sha256']==incremental['amf_binary_sha256'],
            source_only_changed_files=plan['source_delta_files'],full_v1_acceptance=fulls['v1-full']['acceptance'],
            incremental_acceptance=acceptance['acceptance'],incremental_pr_verify_full_check=acceptance['pr_verify_full_check'],
            full_v1_runner_wall=fulls['v1-full']['wall_seconds'],incremental_runner_wall=inc['wall_seconds'],
            full_compatibility_placement=full_place,full_wall_minus_vivado_placement=fulls['v1-full']['wall_seconds']-full_place,
            full_route_seconds=full_route,incremental_joint_route_seconds=joint_route,
            route_ratio=joint_route/full_route,route_70pct_pass=joint_route<=.7*full_route,
            max_branch_seconds=max(w['seconds'] for w in inc['workers'].values()),
            measured_branch_span_seconds=inc['worker_span_seconds'],raw_chain_seconds=time.time()-state['started_epoch'],
            scheduling=state['scheduling'],
            original_chain_start_epoch=plan['original_chain_start_epoch'],
            actual_chain_span_from_original_start_seconds=time.time()-plan['original_chain_start_epoch'],
            fresh_v0_once_cost_seconds=fulls['v0-full']['wall_seconds']+fulls['v0-full'].get('preparation_recovery',{}).get('parent_wall_seconds',0),
            v0_recovery=fulls['v0-full'].get('preparation_recovery'),full_stage_times=full_times,joint_stage_times=joint_times,
            outputs=acceptance['outputs'],dcp=str(incout/'joint/joint_routed.dcp'),
            note='Main walls include mandatory correctness checks and bitstreams; detailed utilization/100 paths run separately. Full V0 once-only static-cache cost is listed separately. Complete stage/profile breakdown remains in producer manifests.')
        recovery=fulls['v1-full']['preparation_recovery']
        inherited=recovery['inherited_prepare_seconds']
        comparison.update(
            v1_preparation_recovery=recovery,
            full_v1_inherited_preparation_seconds=inherited,
            full_v1_reconstructed_main_seconds=inherited+fulls['v1-full']['wall_seconds'],
            full_v1_reconstructed_minus_vivado_place=inherited+fulls['v1-full']['wall_seconds']-full_place,
            reconstruction_note='Sum of measured inherited prepare and new recovery run; not a single observed cold-run wall. Original failed AMF, repair/build and queue overhead remain separate.',
            historical_failed_v1_amf_seconds=recovery['inherited_failed_amf_seconds'],
            static_cache_producer_binary_sha256=fulls['v0-full']['binary_sha256'],
            static_cache_producer_same_binary=fulls['v0-full']['binary_sha256']==incremental['amf_binary_sha256'])
        save(out/'comparison.json',comparison)
        save(Path(plan['repair_acceptance_record']),dict(
            accepted=False,state='experiment_completed_pending_evidence_review',
            comparison=str(out/'comparison.json'),comparison_sha256=sha(out/'comparison.json'),
            reminder_requires_real_acceptance=True))
        state['state']='completed'
    except Exception as e:
        state.update(state='failed',error=repr(e),traceback=traceback.format_exc())
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.time()-state['started_epoch'])
        save(out/'manifest.json',state)
    # Dispatch full reports after all measured implementation runs to avoid contention.
    reports=[]
    for name in ('v0-full','v1-full'):
        source=Path(plan['runs'][name]);mp=source/'manifest.json'
        if not mp.exists() or json.loads(mp.read_text()).get('state')!='completed':continue
        target=out/(name+'-background')
        with (out/(name+'-background-controller.log')).open('w') as log:
            p=subprocess.Popen([sys.executable,__file__,'--background-full',str(source),'--output',str(target)],
                cwd=out,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        reports.append(dict(name=name,pid=p.pid,process_group=p.pid,output=str(target),excluded_from_main_flow=True))
    save(out/'background-dispatch.json',reports)
    return 0 if state['state']=='completed' else 2

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--background-full')
    a=p.parse_args()
    if a.background_full:background_full(a.background_full,Path(a.background_full)/'manifest.json',a.output)
    else:raise SystemExit(run(a.output))
