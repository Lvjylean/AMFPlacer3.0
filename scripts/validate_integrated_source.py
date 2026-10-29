#!/usr/bin/env python3
"""Reproducible E2 + incremental regression suite; no benchmark routing."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PYTHON=[
 'scripts/test_e2_incremental.py','scripts/test_sll_inventory_query.py','scripts/test_incremental_config.py',
 'scripts/test_device_placement_config.py','scripts/test_boundary_strategy_config.py',
 'scripts/test_clock_guidance_batch.py','scripts/test_parallel_safety.py',
 'scripts/test_optimized_rm_contract.py','scripts/test_background_reports.py',
 'scripts/test_dfx_no_static_containment.py','scripts/test_dfx_static_membership_names.py',
 'tests/test_native_cache_contract.py','tests/test_dfx.py','tests/dfx/test_local_resources.py',
 'tests/dfx/test_input_scope.py','tests/dfx/test_multi_contract.py','tests/dfx/test_import_groups.py',
 'tests/dfx/test_synthesis_resume.py','tests/dfx/test_static_audit_mode.py',
 'tests/test_full_flow.py','tests/test_import_acceptance.py',
 'tests/test_paper_boundary_config.py','tests/test_srl_cascades.py']
CPP=['checkSllQuantity','checkSllAdaptivePolicy','checkSllVirtualModel','checkSllDensity',
 'checkSllTileInitialization','checkSllColumnInitialization','checkSllNoSplit','checkSllTransientCapacity',
 'checkSllQpProjection','checkSllWireCache','checkSllTimingIndex',
 'checkSllDensityEquivalence','checkSllPlannedTimingCapacity','checkSllOutputTail',
 'checkNativeStaticCache','checkNativePlacementRepair','checkBoundarySitePolicy',
 'checkNativeFfPinRepair','checkIncrementalConfig','checkDFXPartitionFloorplan',
 'checkDevicePlacementPolicy']
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--binary-dir',type=Path,required=True)
 ap.add_argument('--fixture-config',type=Path,required=True)
 ap.add_argument('--incremental-config',type=Path,required=True)
 ap.add_argument('--output',type=Path,required=True)
 args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 b=args.binary_dir.resolve();results=[];start=time.monotonic()
 manifest={'schema':1,'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
           'binary_dir':str(b),'binary_sha256':hashlib.sha256((b/'AMFPlacer').read_bytes()).hexdigest(),
           'fixture_sha256':hashlib.sha256(args.fixture_config.read_bytes()).hexdigest(),
           'incremental_fixture_sha256':hashlib.sha256(args.incremental_config.read_bytes()).hexdigest(),
           'stages':results,'state':'running'}
 def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 def run(name,cmd):
  before=time.monotonic();log=out/(name+'.log')
  with log.open('w') as f:p=subprocess.run([str(x) for x in cmd],cwd=ROOT,
   env=dict(os.environ,OMP_NUM_THREADS='4'),stdout=f,stderr=subprocess.STDOUT,timeout=900)
  text=log.read_text(errors='replace');m=re.search(r'Ran (\d+) tests?',text)
  results.append({'name':name,'command':[str(x) for x in cmd],'exit_code':p.returncode,
                  'seconds':time.monotonic()-before,'tests':int(m[1]) if m else None})
  save();print(name,p.returncode,flush=True)
  return p.returncode==0
 save()
 targets=CPP+['checkSllTimingCandidates','checkDieFirstSpreading','checkSllJointIntegration',
              'checkIncrementalSllIntegration','checkSllPhysicalFixture']
 if not run('compile-tests',['cmake','--build',b,'--parallel','8','--target',*targets]):
  manifest['state']='failed';save();return 1
 for i,p in enumerate(PYTHON):run('python-'+str(i)+'-'+Path(p).stem,['python3',p])
 for t in CPP:run(t,[b/t])
 cfg=json.loads(args.fixture_config.read_text())
 run('timing-candidate-equivalence',[b/'checkSllTimingCandidates',cfg['SllChannelFile']])
 run('die-first-spreading',[b/'checkDieFirstSpreading',args.fixture_config])
 run('full-e2-qp-sta',[b/'checkSllJointIntegration',args.fixture_config,out/'full-qp','tile'])
 run('incremental-e2-qp',[b/'checkIncrementalSllIntegration',args.incremental_config,out/'incremental-qp'])
 caps=json.loads(subprocess.check_output([b/'AMFPlacer','--capabilities'],text=True))
 required={'incremental_sll_planning_schema':1,'virtual_sll_resource_schema':1,
           'paired_sll_timing_schema':1,'sll_output_long_delay_schema':1,
           'native_static_cache_schema':2,'native_local_pin_access_schema':2,
           'device_placement_policy_schema':1}
 manifest['capabilities']=caps
 manifest['required_capabilities_passed']=all(caps.get(k)==v for k,v in required.items())
 manifest['wall_seconds']=time.monotonic()-start
 manifest['state']='passed' if manifest['required_capabilities_passed'] and all(x['exit_code']==0 for x in results) else 'failed'
 manifest['python_test_count']=sum(x['tests'] or 0 for x in results)
 manifest['passed_stages']=sum(x['exit_code']==0 for x in results)
 save();return 0 if manifest['state']=='passed' else 1
if __name__=='__main__':raise SystemExit(main())
