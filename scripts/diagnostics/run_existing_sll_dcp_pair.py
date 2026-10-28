#!/usr/bin/env python3
"""Route the two user-selected existing GETRF DCPs once each, serially."""
import argparse,datetime,hashlib,json,os,pathlib,shutil,subprocess,sys,time,zipfile
P=pathlib.Path
REPO=P(__file__).resolve().parents[2]
VIVADO='/Projects/Xilinx26/2026.1/Vivado/bin/vivado'
def now():return datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat()
def sha(p):
 h=hashlib.sha256()
 with P(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def save(p,v):
 q=p.with_suffix(p.suffix+'.tmp');q.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n');q.replace(p)
def resource():
 d={s.split(':')[0]:int(s.split()[1]) for s in P('/proc/meminfo').read_text().splitlines()}
 return {'at':now(),'mem_available_gib':d['MemAvailable']/1024**2,'swap_free_gib':d['SwapFree']/1024**2,'loadavg':os.getloadavg(),'disk_free_gib':shutil.disk_usage(REPO).free/1024**3}
def start():
 stamp=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d-%H%M%S-%f')
 ident='getrf-e2-sll-route-pair-'+stamp
 cmp=REPO/'experiments/comparisons'/ident;cmp.mkdir(parents=True,exist_ok=False);(cmp/'inputs').mkdir()
 source=REPO/'experiments/runs'
 cases=[('sll-fixed',source/'getrf-sll-terminal-import-20261028-06/reports/getrf_placed.dcp','9ccb0d061876d493aada21aae90ef7f17aaac7a1052d41e8a6597afc19df5521'),('sll-free',source/'getrf-sll-terminal-import-20261028-01/reports/getrf_before_sll.dcp','45678bc9b73ee269f15c2be9ccd270d7c22b1bc4e49a768937e3bc0077986800')]
 plan=P('/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-dense-density/repo/experiments/runs/getrf-u250-e2-placement-only-20261028-045411-729709/placement/sll-plan.tcl')
 for file in [P(__file__),P(__file__).with_name('route_existing_sll_dcp.tcl'),REPO/'scripts/sll_routes.tcl']:
  shutil.copy2(file,cmp/'inputs'/file.name)
 info={'created':now(),'comparison':str(cmp),'repo':str(REPO),'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),'source_status':subprocess.check_output(['git','status','--short'],cwd=REPO,text=True),'amf_executed':False,'amf_build':'Existing frozen E2 placement reused; no AMF binary run or rebuild.','clock_period_ns':10,'threads':4,'route_command':'route_design','incremental':False,'placement_optimization':False,'attempts_per_case':1,'order':['sll-fixed','sll-free'],'scope':'Route both existing DCPs as-is; differences in endpoint locks are measured, not normalized.','dcp_storage':'server-only','plan':str(plan),'inputs':{str(plan):sha(plan)},'cases':[]}
 for mode,input,expected in cases:
  actual=sha(input);assert actual==expected,(input,actual)
  run=REPO/'experiments/runs'/(ident+'-'+mode)
  info['inputs'][str(input)]=actual;info['cases'].append({'mode':mode,'input':str(input),'sha256':actual,'run':str(run)})
 for f in (cmp/'inputs').iterdir():info['inputs'][str(f)]=sha(f)
 (cmp/'source.diff').write_bytes(subprocess.check_output(['git','diff'],cwd=REPO))
 save(cmp/'manifest.json',info);save(cmp/'status.json',{'state':'prepared','at':now()})
 with (cmp/'controller.log').open('x') as log:
  p=subprocess.Popen([sys.executable,str(P(__file__).resolve()),'--worker',str(cmp)],cwd=cmp,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 save(cmp/'controller.json',{'pid':p.pid,'started':now(),'script':str(P(__file__).resolve())})
 print(json.dumps({'comparison':str(cmp),'pid':p.pid,'cases':info['cases']},indent=2))
def worker(cmp):
 manifest=json.loads((cmp/'manifest.json').read_text())
 with (cmp/'execution-intent.json').open('x') as f:json.dump({'at':now(),'serial':True,'attempts_per_case':1},f)
 for path,h in manifest['inputs'].items():assert sha(path)==h,path
 statuses=[]
 for case in manifest['cases']:
  run=P(case['run']);run.mkdir(parents=True,exist_ok=False);reports=run/'reports';reports.mkdir()
  cmd=[VIVADO,'-mode','batch','-notrace','-nojournal','-log',str(run/'vivado-internal.log'),'-source',str(cmp/'inputs/route_existing_sll_dcp.tcl'),'-tclargs',str(reports),case['input'],case['mode'],str(cmp/'inputs/sll_routes.tcl'),manifest['plan']]
  info={**case,'comparison':str(cmp),'started':now(),'command':cmd,'source_commit':manifest['source_commit'],'script_sha256':sha(cmp/'inputs/route_existing_sll_dcp.tcl'),'resources_at_start':resource()}
  save(run/'manifest.json',info);save(run/'status.json',{'state':'running','at':now()});save(cmp/'status.json',{'state':'running','active':case['mode'],'completed':statuses,'at':now()})
  env=dict(os.environ);env.setdefault('LM_LICENSE_FILE','/Projects/jinyang/licenses/vivado_2026.lic')
  begin=time.monotonic()
  with (run/'vivado.log').open('x') as log,(run/'resources.jsonl').open('x') as res:
   p=subprocess.Popen(cmd,cwd=run,stdout=log,stderr=subprocess.STDOUT,env=env)
   info['pid']=p.pid;save(run/'manifest.json',info)
   while p.poll() is None:
    res.write(json.dumps(resource())+'\n');res.flush();time.sleep(10)
   rc=p.wait()
  status={'state':'completed' if rc==0 else 'failed','exit_code':rc,'elapsed_seconds':time.monotonic()-begin,'finished':now(),'mode':case['mode'],'run':str(run)}
  obs=reports/'observations.tsv'
  if obs.exists():
   pairs=dict(line.split('\t',1) for line in obs.read_text().splitlines()[1:] if '\t' in line);status['observations']=pairs
  dcp=reports/'route_attempt.dcp'
  if dcp.exists():
   try:
    with zipfile.ZipFile(dcp) as z:bad=z.testzip()
    status['checkpoint']={'path':str(dcp),'sha256':sha(dcp),'bytes':dcp.stat().st_size,'crc_passed':bad is None}
   except Exception as e:status['checkpoint_error']=str(e)
  status['input_hash_unchanged']=sha(P(case['input']))==case['sha256']
  save(run/'status.json',status);statuses.append(status)
  print(json.dumps(status),flush=True)
 save(cmp/'status.json',{'state':'attempts_finished','finished':now(),'cases':statuses,'all_route_commands_succeeded':all(s.get('observations',{}).get('route_return_code')=='0' for s in statuses)})
if __name__=='__main__':
 a=argparse.ArgumentParser();g=a.add_mutually_exclusive_group(required=True);g.add_argument('--start',action='store_true');g.add_argument('--worker',type=P);args=a.parse_args()
 if args.start:start()
 else:worker(args.worker)
