#!/usr/bin/env python3
"""Single joint backend; consumes existing AMF placed checkpoints, never runs AMF."""
import argparse,csv,datetime,hashlib,json,os,shutil,subprocess,sys,time
from pathlib import Path
import run_dfx as single
import run_dfx_multi as flow
from common_static_contract import verify as verify_common_static
D=Path(__file__).resolve().parents[1]
R=D.parents[2]
OUT=None
STATIC_SHELL=None
INPUTS_ONLY=False
WORKERS={
 'a':R/'experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-incremental-099-05-staticanchor18-recovery/workers/a',
 'b':R/'experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-incremental-099-04-metadatafix16/workers/b'}
def sha(p):return single.sha(p)
def save(p,d):p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
def preflight():
 global WORKERS
 job=flow.load(D/'configs/joint-099-20ns.json')
 assert job['clock_period_ns']==20 and job['vivado_threads']==4
 assert job['backend_place_mode']=='route-only' and job['static_routing_containment'] is False
 assert job['static_placement_audit']=='skip'
 WORKERS={k:Path(v) for k,v in job['joint_backend_source_workers'].items()}
 proofs={}
 modules={}
 cache=None;cache_proof=None;cache_times=[];cache_retention=[]
 for part in job['partitions']:
  k=part['id'];p=WORKERS[k];m=json.loads((p/'manifest.json').read_text());cfg=json.loads((p/'amf_config.json').read_text())
  expected=job.get('source_worker_hashes',{}).get(k)
  if expected:assert sha(p/'manifest.json')==expected['manifest'] and m['binary_sha256']==expected['binary']
  if m['state']=='amf_placed':assert m['amf_import_acceptance']['amf_loc_bel_exact']
  else:assert m['state']=='completed' and m['acceptance']['implementation_verified']
  for key in ['part','clock_name','clock_period_ns']:
   assert m['job'][key]==job[key]
  assert m['job']['active_partition']==part['active_partition'] and m['job']['pblock']==part['pblock']
  assert cfg['ProPrimitiveTiming']=='false' and float(cfg['ClockPeriod'])==20 and int(cfg['jobs'])==12
  assert sha(p/'imported.dcp')==m['outputs']['imported.dcp']['sha256']
  if expected:assert sha(p/'imported.dcp')==expected['imported']
  with (p/'cells.tsv').open() as f:active={x['cell'] for x in csv.DictReader(f,delimiter='\t') if x['active']=='1'}
  golden=single.placement_rows(p/'placement/DumpCLBPacking-first-0.tcl',active,set((p/'allowed_sites.txt').read_text().splitlines()),set((p/'blocked_sites.txt').read_text().splitlines()))
  rows={}
  for line in (p/'placement.tsv').read_text().splitlines():
   c,target=line.split('\t');assert c not in rows and c.startswith(part['active_partition']+'/');rows[c]=target
  assert len(rows)==len(golden)==len(active)>0 and rows==golden
  if m['state']=='amf_placed':
   module=p/'rm_amf_placed.dcp';module_sha=m['outputs']['rm_amf_placed.dcp']['sha256'];provenance='worker AMF import-only artifact'
  else:
   if cache_proof is None:
    cache=Path(job['amf_placed_module_cache_run'])
    cache_proof=json.loads((cache/'preflight.json').read_text())
    assert sha(cache/'preflight.json')==job['module_cache_preflight_sha256']
    assert sha(cache/'inputs/dfx_joint_backend.tcl')==job['module_cache_producer_tcl_sha256']
    cache_times=[v.split('\t') for v in (cache/'stage_times.tsv').read_text().splitlines()]
    cache_retention=(cache/'placement_retention_counts.tsv').read_text().splitlines()
   module=cache/'prepared_rm'/(k+'_amf_placed.dcp');module_sha=job['amf_placed_module_sha256'][k];provenance='reused completed AMF-only extraction, before parent placer/router'
   assert cache_proof['workers'][k]['run']==str(p)
   assert cache_proof['workers'][k]['files']['imported.dcp']['sha256']==sha(p/'imported.dcp')
   for stage in ['certify_amf_placement_'+k,'extract_amf_placed_'+k]:assert any(v[0]==stage and v[2]=='0' for v in cache_times)
   assert ('source_imported\t'+k+'\t'+str(len(rows))+'\t'+str(len(rows))+'\t0') in cache_retention
  assert module.is_file() and sha(module)==module_sha
  modules[k]=dict(path=str(module),sha256=module_sha,bytes=module.stat().st_size,provenance=provenance)
  files=['manifest.json','imported.dcp','placement.tsv','placement/DumpCLBPacking-first-0.tcl','cells.tsv','allowed_sites.txt','blocked_sites.txt','amf_config.json','amf_profile.tsv','inputs/dfx_backend.tcl']
  proofs[k]=dict(run=str(p),files={n:dict(sha256=sha(p/n),bytes=(p/n).stat().st_size) for n in files},binary_sha256=m['binary_sha256'],active_cells=len(rows),amf_dump_table_exact=True,placement_scope=m['amf_placement_scope'],inherited_stages=m['stages'])
 shell=Path(job['shell_run']);parent=json.loads((shell/'manifest.json').read_text())
 assert parent['state']=='completed' and parent['acceptance']['implementation_verified']
 assert sha(shell/'manifest.json')=='5ba109780af30bd5158ed175f6a9711f5e80118f7c5062ef3abdb7a0904fd72c'
 assert sha(shell/'base_routed.dcp')==parent['outputs']['base_routed.dcp']=='04be98d2dd7bd3c759c2a2d1b06f5d053631c8abd86ca7304c3464de66c1cc01'
 common_dir=STATIC_SHELL or Path(job['common_static_shell_run'])
 common=None if INPUTS_ONLY else verify_common_static(job,common_dir,shell/'base_routed.dcp')
 job['common_static_shell_run']=str(common_dir)
 version=subprocess.run([job['vivado'],'-version'],text=True,capture_output=True,check=True,timeout=30).stdout
 assert 'vivado v2026.1' in version.lower()
 return job,dict(checked_at_hkt=datetime.datetime.now().isoformat(),complete_backend_preflight=not INPUTS_ONLY,common_static_validation_pending=INPUTS_ONLY,vivado_placement_required=False,modules=modules,common_static_shell=dict(directory=str(common_dir),contract_sha256=None if INPUTS_ONLY else sha(common_dir/'common_static_shell.json'),contract=common),workers=proofs,shell_manifest_sha256=sha(shell/'manifest.json'),base_routed_sha256=sha(shell/'base_routed.dcp'),vivado_version=version,source_hashes={str(f.relative_to(D)):sha(f) for f in sorted(D.rglob('*')) if f.is_file() and f.suffix in ('.py','.tcl','.json')},amf_rerun=False,old_worker_routes_consumed=False,strict_same_binary_reference=False,binary_qualification='A fixed-neighbor binary differs from B/full reference; B same-input replay proved placement equivalence only for this input')
def main():
 global OUT,STATIC_SHELL,INPUTS_ONLY
 ap=argparse.ArgumentParser();ap.add_argument('--preflight-only',action='store_true');ap.add_argument('--inputs-only-preflight',action='store_true');ap.add_argument('--output');ap.add_argument('--static-shell-run');args=ap.parse_args()
 STATIC_SHELL=Path(args.static_shell_run).resolve() if args.static_shell_run else None
 INPUTS_ONLY=args.inputs_only_preflight
 if not (args.preflight_only or INPUTS_ONLY) and not args.output:ap.error('--output is required for a new joint run')
 OUT=Path(args.output).resolve() if args.output else None
 if args.preflight_only or INPUTS_ONLY:
  job,proof=preflight();print(json.dumps(proof));return
 assert not OUT.exists(),'Joint run already exists; inspect instead of relaunch'
 OUT.mkdir();(OUT/'inputs').mkdir();(OUT/'logs').mkdir()
 start=time.monotonic()
 state=dict(schema='amf-two-placed-rms-joint-backend-v1',state='preflight',started_at_hkt=datetime.datetime.now().isoformat(),runner_pid=os.getpid(),process_group=os.getpgrp(),workflow='both-empty-RP locked static + exact AMF module placements + one joint route, no Vivado placement',amf_rerun=False,worker_routes_consumed=False,stages=[])
 def commit():
  save(OUT/'manifest.json',state);save(OUT/'status.json',{k:state.get(k) for k in ('state','stage','error','runner_pid','process_group')})
 commit()
 try:
  job,proof=preflight();save(OUT/'preflight.json',proof);state['preflight']=proof
  names=['dfx_joint_backend.tcl','dfx_multi_common.tcl','dfx_common.tcl','dfx_boundary_compatibility.tcl','dfx_common_static_shell.tcl']
  for n in names:shutil.copy2(D/'scripts'/n,OUT/'inputs'/n)
  common_dir=Path(job['common_static_shell_run'])
  metadata=OUT/'inputs/common_static';metadata.mkdir()
  for n in ['common_static_shell.json']+[p['id']+'_common_static_boundary.tcldata' for p in job['partitions']]:shutil.copy2(common_dir/n,metadata/n)
  runtime_workers={}
  for k in WORKERS:
   dest=OUT/'inputs'/('worker_'+k);dest.mkdir();runtime_workers[k]=dest
   for n in ['placement.tsv','blocked_sites.txt']:shutil.copy2(WORKERS[k]/n,dest/n)
  extra=dict(reference_checkpoint=str(Path(job['shell_run'])/'base_routed.dcp'),common_static_checkpoint=str(common_dir/'common_static_locked.dcp'),common_static_metadata=str(metadata),worker_a=str(runtime_workers['a']),worker_b=str(runtime_workers['b']),rm_a_checkpoint=proof['modules']['a']['path'],rm_b_checkpoint=proof['modules']['b']['path'],expected_cells_a=proof['workers']['a']['active_cells'],expected_cells_b=proof['workers']['b']['active_cells'])
  settings=flow.settings(job,OUT,extra)
  state.update(state='running',stage='joint_backend',job=job,script_hashes={n:sha(OUT/'inputs'/n) for n in names},settings_sha256=sha(settings));commit()
  step=flow.vivado(job,OUT,'dfx_joint_backend.tcl',settings)
  state['stages'].append(step);commit()
  if step['exit_code']:raise RuntimeError('Joint backend failed; preserve full Tcl log and checkpoints')
  acceptance=flow.acceptance(OUT,'joint');acceptance['pr_verify_full_check']=(OUT/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
  state['acceptance']=acceptance
  if not acceptance['implementation_verified'] or not acceptance['timing_met'] or not acceptance['pr_verify_full_check']:raise RuntimeError('Joint correctness/timing acceptance failed')
  outputs=['joint_amf_placed.dcp','joint_routed.dcp','full.bit','a_partial.bit','b_partial.bit','vivado_placement_not_executed.txt']
  state['outputs']={}
  for n in outputs:
   f=OUT/n
   if not f.is_file() or not f.stat().st_size:raise RuntimeError('Missing joint output '+n)
   state['outputs'][n]=dict(path=str(f),sha256=sha(f),bytes=f.stat().st_size)
  times={line.split('\t')[0]:float(line.split('\t')[1]) for line in (OUT/'stage_times.tsv').read_text().splitlines()}
  rt=times['vivado_route'];place=0.0
  if 'vivado_place' in times:raise RuntimeError('Route-only contract violated: Vivado placement executed')
  state['performance']=dict(joint_route_seconds=rt,reference_full_v1_route_seconds=3357.088,joint_route_fraction=rt/3357.088,joint_route_70pct_passed=rt<=2349.9616,vivado_compatibility_place_seconds=place,vivado_placement_executed=False,placement_execution_note='No Vivado placement invoked; zero is absence of execution, not a subtracted estimate',amf_result_reused=True,strict_same_binary_reference=False,prior_experiment_70pct_failure_preserved=True)
  state.update(state='completed',stage='await_final_report')
 except Exception as error:
  state.update(state='failed',error=str(error));raise
 finally:
  state['raw_joint_run_wall_seconds']=time.monotonic()-start
  state['finished_at_hkt']=datetime.datetime.now().isoformat()
  if 'performance' in state:state['adjusted_wall_excluding_vivado_placement_seconds']=state['raw_joint_run_wall_seconds']-state['performance']['vivado_compatibility_place_seconds']
  commit()
if __name__=='__main__':main()

