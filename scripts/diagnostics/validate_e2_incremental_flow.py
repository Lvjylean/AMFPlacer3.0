#!/usr/bin/env python3
"""Synthetic two-RP validation. Module DCPs retain interface/clock stubs; internal nets are unrouted."""
from pathlib import Path
import argparse,json,sys,os,subprocess,time,zipfile,re,hashlib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from e2_incremental import prepare,publish,bind,digest
def run(base,model,binary,out):
 out.mkdir(parents=True,exist_ok=False)
 cfg=json.loads(model.read_text())
 job={'amf_binary':str(binary/'AMFPlacer'),'vivado':'/Projects/Xilinx26/2026.1/Vivado/bin/vivado',
 'common_static_shell_run':str(base),'vivado_threads':4,
 'partitions':[{'id':k,'active_partition':k,'pblock':'pb_'+k} for k in ['a','b']]}
 manifest={'schema':1,'state':'running','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
 'binary_sha256':digest(binary/'AMFPlacer'),'base_sha256':digest(base/'base_routed.dcp'),
 'static_sha256':digest(base/'common_static_locked.dcp'),'model_sha256':digest(model),'stages':[]}
 def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 save();started=time.monotonic()
 try:
  os.environ['LM_LICENSE_FILE']='/Projects/jinyang/licenses/vivado_2026.lic'
  contract=prepare(job,cfg,out/'reservations',ROOT/'scripts');manifest['reservation_contract']=contract;save()
  used=(out/'reservations/sll-used.tsv').read_text().splitlines()
  if len(used)<=1:raise RuntimeError('Fixture must retain at least one occupied static SLL')
  with zipfile.ZipFile(cfg['vivado extracted device information file']) as z:
   modeled=set(re.findall(r'^site=> (\S+)',z.read(z.namelist()[0]).decode(),re.M))
  for k in ['a','b']:
   worker=out/('worker-'+k);worker.mkdir()
   c=dict(cfg);c.update(SllFixtureQpSteps='2',IncrementalCompilation='true',SllConstraintWorkers='2',
    ClockRegionColumnStretch='false',visualization='false')
   c['DFX active partition']=k;c['DFX input scope']='rm-boundary-cone-v1';c.pop('DFX placement mode',None)
   (worker/'allowed.txt').write_text('\n'.join(sorted(set((base/(k+'-allowed.txt')).read_text().splitlines()) & modeled))+'\n')
   (worker/'blocked.txt').write_text('');(worker/'static.tsv').write_text('cell\tsite\tbel\n')
   c.update({'DFX allowed sites file':str(worker/'allowed.txt'),'DFX blocked sites file':str(worker/'blocked.txt'),'DFX static placement file':str(worker/'static.tsv')})
   text=''
   for name,pin,direction in [('source','Q','OUT'),('local_ff','D','IN'),('remote','D','IN')]:
    text+='curCell=> '+k+'/'+name+' type=> FDRE\n'
    text+='   pin=> '+k+'/'+name+'/'+pin+' refpin=> '+pin+' dir=> '+direction+' net=> '+k+'/s drivepin=> '+k+'/source/Q\n'
   with zipfile.ZipFile(worker/'netlist.zip','w',zipfile.ZIP_DEFLATED) as z:z.writestr('netlist',text)
   (worker/'clocks').write_text('')
   c['vivado extracted design information file']=str(worker/'netlist.zip');c['clock file']=str(worker/'clocks')
   bind({'e2_sll_reservation':contract['reservations'][k]},c)
   (worker/'config.json').write_text(json.dumps(c,indent=2))
   before=time.monotonic()
   cmd=[str(binary/'checkSllPhysicalFixture'),str(worker/'config.json'),str(base/(k+'-cells.tsv')),str(worker/'placement')]
   with (worker/'amf.log').open('w') as log:p=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,OMP_NUM_THREADS='4'))
   manifest['stages'].append({'name':'amf-'+k,'command':cmd,'exit_code':p.returncode,'seconds':time.monotonic()-before});save()
   if p.returncode:raise RuntimeError('AMF fixture failed')
   if 'nets=1 virtual_cells=1 actual_cells=3' not in (worker/'amf.log').read_text():
    raise RuntimeError('Fixture must exercise a real shared cross-die plan')
   publish(c,worker)
  backend=Path(__file__).with_suffix('.tcl');(out/'backend.tcl').write_text(backend.read_text())
  before=time.monotonic();cmd=[job['vivado'],'-mode','batch','-notrace','-source',str(out/'backend.tcl'),'-tclargs',str(out),str(base),str(ROOT/'scripts')]
  with (out/'backend.log').open('w') as log:p=subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT)
  manifest['stages'].append({'name':'joint-route-and-audit','command':cmd,'exit_code':p.returncode,'seconds':time.monotonic()-before});save()
  if p.returncode:raise RuntimeError('Joint backend failed; inspect backend.log')
  route=(out/'route-status.rpt').read_text()
  def count(label):
   m=re.search(re.escape(label)+r'\.*\s*:\s*(\d+)',route)
   if not m:raise RuntimeError('Missing route metric: '+label)
   return int(m.group(1))
  routable=count('# of routable nets');routed=count('# of fully routed nets');errors=count('# of nets with routing errors')
  if errors or routable!=routed:raise RuntimeError('Incomplete or illegal final routing')
  audit=(out/'sll/sll-path-audit.tsv').read_text().splitlines()[1:]
  if len(audit)!=4 or any(row.split('\t')[-1]!='1' for row in audit):raise RuntimeError('Receiver SLL audit failed')
  drc=(out/'drc.rpt').read_text()
  if re.search(r'\|\s*Error\s*\|',drc):raise RuntimeError('DRC error')
  if not (out/'pr-verify-passed.txt').exists():raise RuntimeError('Missing full PR verification')
  manifest['metrics']={'routable_nets':routable,'routed_nets':routed,'routing_errors':errors,'receiver_paths_audited':len(audit),'pr_verify_full_check':True}
  manifest['state']='passed'
 except Exception as e:manifest.update(state='failed',error=str(e));raise
 finally:manifest['wall_seconds']=time.monotonic()-started;save()
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--model',type=Path,required=True)
 p.add_argument('--binary-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 run(a.base.resolve(),a.model.resolve(),a.binary_dir.resolve(),a.output.resolve())
