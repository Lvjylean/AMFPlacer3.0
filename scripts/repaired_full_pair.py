"""Adopt completed V0, recover V1 preparation, then let the finite chain continue."""
import json,os,subprocess,sys,time
from pathlib import Path
from run_dfx import sha,save
from parallel_runtime import require_memory,verify_hashes

def run_full_pair(D,out,plan,state):
 v0=Path(plan['runs']['v0-full']);v1=Path(plan['runs']['v1-full'])
 if sha(v0/'manifest.json')!=plan['completed_v0_manifest_sha256']:raise ValueError('Completed V0 metadata changed')
 full0=json.loads((v0/'manifest.json').read_text())
 if full0['state']!='completed' or not full0['acceptance']['implementation_verified']:raise ValueError('V0 is not accepted')
 require_memory(80)
 job=json.loads((D/'configs/v1-full.json').read_text())
 cmd=[sys.executable,str(D/'scripts/run_dfx_full.py'),'--config',str(D/'configs/v1-full.json'),
      '--output',str(v1),'--binary',job['amf_binary']]
 env=dict(os.environ,LD_LIBRARY_PATH=job['amf_runtime_lib']+':'+os.environ.get('LD_LIBRARY_PATH',''))
 state['scheduling']={'mode':'completed_v0_then_repaired_v1_then_parallel_incremental',
   'historical_overlap':'Original V0/V1 full-37 overlapped on the same host; old timings retained separately',
   'v0_rerun':False,'v1_preparation_inherited':True,'monitoring_automation':'PAUSED'}
 state['steps'].append({'name':'v0-full','state':'completed','adopted':True,'manifest_sha256':sha(v0/'manifest.json')})
 rec={'name':'v1-full','state':'running','command':cmd,'started_epoch':time.time()};state['steps'].append(rec)
 state['state']='v1-full';save(out/'manifest.json',state)
 verify_hashes({str(D/k):v for k,v in plan['files'].items()})
 with (out/'v1-full.log').open('w') as log:
  p=subprocess.Popen(cmd,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT)
  rec['pid']=p.pid;save(out/'manifest.json',state);rc=p.wait()
 rec.update(exit_code=rc,ended_epoch=time.time(),state='completed' if rc==0 else 'failed')
 rec['wall_seconds']=rec['ended_epoch']-rec['started_epoch'];save(out/'manifest.json',state)
 if rc:raise RuntimeError('Repaired V1 full failed; preserve evidence; do not retry automatically')
 full1=json.loads((v1/'manifest.json').read_text())
 if full1['state']!='completed':raise ValueError('Repaired V1 incomplete')
 return {'v0-full':full0,'v1-full':full1}
