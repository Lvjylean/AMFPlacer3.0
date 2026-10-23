#!/usr/bin/env python3
"""Finite routed-assembly queue; consumes verified workers, never starts AMF."""
import datetime, importlib.util, json, shutil, sys, time
from pathlib import Path
deployment=Path("/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/deployments/full-amf-dfx-static-neighbor-recovery-18")
out=Path("/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-incremental-099-05-staticanchor18-recovery")
replay=Path("/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/preflight/20261015-dfx-static-neighbor-b-replay-01")
sys.path.insert(0,str(deployment/'scripts'))
spec=importlib.util.spec_from_file_location('dfx_multi',deployment/'scripts/run_dfx_multi.py')
flow=importlib.util.module_from_spec(spec);spec.loader.exec_module(flow)
state={'schema':'amf-dfx-recovery-assembly-v1','state':'waiting_verified_workers','started_at_hkt':datetime.datetime.now().isoformat(),'scope':'Assembly only; no place_design, route_design or AMF rerun','worker_sources':{'a':str(out/'workers/a'),'b':str((out/'workers/b').resolve())},'stages':[]}
def save():
 state['checked_at_hkt']=datetime.datetime.now().isoformat()
 flow.save(out/'assembly-controller-status.json',state)
 flow.save(out/'status.json',{'state':state['state'],'stage':state.get('stage','waiting_verified_workers'),'error':state.get('error')})
t=time.monotonic();save()
try:
 job=flow.load(deployment/'configs/getrf-incremental-099-20ns.json')
 assert job['clock_period_ns']==20 and not flow.static_containment_enabled(job)
 shell,parent=flow.check_shell(job)
 proof=json.loads((replay/'manifest.json').read_text())
 if proof['state']!='completed' or not proof['exact_active_placement_equivalence'] or not proof['input_files_unchanged'] or not proof['fixed_context_verified']:
  raise RuntimeError('B replay did not validate reuse')
 state['b_replay_proof_sha256']=flow.sha(replay/'manifest.json')
 state['b_replay_scope']='Same2956cell placement; new fixed-neighbor branch unused; actual B backend is retained old-binary run'
 state['parent_shell']=str(shell);state['parent_manifest_sha256']=flow.sha(shell/'manifest.json')
 state['job']=job
 state['baseline_full_v1']='getrf-full-amf-uncontained-srlfix-20ns-v1-099-03'
 state['worker_checks']={}
 deadline=time.monotonic()+8*3600
 while True:
  ready=True
  for name in ('a','b'):
   p=out/'workers'/name
   if not (p/'manifest.json').exists():ready=False;continue
   m=json.loads((p/'manifest.json').read_text())
   if m['state']=='failed':raise RuntimeError('Worker '+name+' failed: '+str(m.get('error')))
   if m['state']!='completed':ready=False;continue
   if not m.get('acceptance',{}).get('implementation_verified'):raise RuntimeError('Worker '+name+' not verified')
   for f in ('routed.dcp','rm_routed.dcp','pr_verify_passed.txt','full.bit','partial.bit'):
    if not (p/f).is_file() or not (p/f).stat().st_size:raise RuntimeError('Missing verified worker artifact '+name+'/'+f)
   state['worker_checks'][name]={'run':str(p.resolve()),'manifest_sha256':flow.sha(p/'manifest.json'),'rm_routed_sha256':flow.sha(p/'rm_routed.dcp'),'timing_met':m['acceptance'].get('timing_met'),'binary_sha256':m.get('binary_sha256')}
  save()
  if ready:break
  if time.monotonic()>deadline:raise RuntimeError('Verified-worker wait exceeded8hours; no process was stopped')
  time.sleep(15)
 assembly=out/'assembly';assembly.mkdir(exist_ok=False)
 (assembly/'inputs').mkdir();(assembly/'logs').mkdir()
 names=('dfx_multi_assemble.tcl','dfx_multi_common.tcl','dfx_common.tcl','dfx_boundary_compatibility.tcl')
 for name in names:shutil.copy2(deployment/'scripts'/name,assembly/'inputs'/name)
 state['assembly_script_hashes']={name:flow.sha(assembly/'inputs'/name) for name in names}
 config=flow.settings(job,assembly,dict(workers=str(out/'workers'),reference_checkpoint=str(shell/'base_routed.dcp')))
 state.update(state='running',stage='assemble_routed_modules');save()
 step=flow.vivado(job,assembly,'dfx_multi_assemble.tcl',config)
 state['stages'].append(step);save()
 if step['exit_code']:raise RuntimeError('Routed assembly failed; retain assembly log')
 acceptance=flow.acceptance(assembly,'assembled')
 acceptance['pr_verify_full_check']=(assembly/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
 state['acceptance']=acceptance
 if not acceptance['implementation_verified'] or not acceptance['pr_verify_full_check']:raise RuntimeError('Combined correctness acceptance failed')
 names=['assembled_routed.dcp','full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]
 state['outputs']={str(assembly/name):{'sha256':flow.sha(assembly/name),'bytes':(assembly/name).stat().st_size} for name in names}
 state.update(state='completed',stage='await_final_performance_report')
except Exception as error:
 state.update(state='failed',error=str(error));raise
finally:
 state['controller_wall_seconds']=time.monotonic()-t
 state['finished_at_hkt']=datetime.datetime.now().isoformat()
 save()
