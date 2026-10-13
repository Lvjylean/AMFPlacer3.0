#!/usr/bin/env python3
"""Read-only audit of real input-cell locations; preserves all experiment outputs."""
import argparse,collections,csv,gzip,hashlib,json,math,re,zipfile
from pathlib import Path
C=collections.Counter
def digest(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def main(root,out):
 idx=json.loads((root/'experiments/evidence/20261007-paper-boundaries-full-01/launch.json').read_text())
 runs=[Path(idx['baseline_run']),Path(idx['run_directory'])]
 cfg=json.loads((runs[1]/'config.json').read_text())
 types={}
 with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
  with z.open(z.namelist()[0]) as f:
   for l in f:
    if l.startswith(b'curCell=>'):
     t=l.decode().split();types[t[1]]=t[3]
 def kind(name):
  t=types[name]
  return 'FF' if t.startswith('FD') else ('LUT' if re.fullmatch(r'LUT[1-6]',t) else 'other')
 kinds={n:kind(n) for n in types}
 members={r['cell']:r['module'] for r in csv.DictReader(open(cfg['external floorplan membership file']),delimiter='\t')}
 regions={r['module']:set(r['clock_regions'].split()) for r in csv.DictReader(open(cfg['external floorplan regions file']),delimiter='\t')}
 sites={}; capacities=C();cr_slr={}
 with zipfile.ZipFile(cfg['vivado extracted device information file']) as z:
  with z.open(z.namelist()[0]) as f:
   for raw in f:
    d=dict(re.findall(r'(\w+)=>\s+(\S+)',raw.decode()))
    x,y=float(d['centerx']),float(d['centery'])
    sites[d['site']]=(x,y,d['clockRegionName'],int(d['slr']))
    cr_slr[d['clockRegionName']]=int(d['slr'])
    if d['sitetype'] in ('SLICEL','SLICEM') and d.get('prohibited','0')=='0':capacities[d['clockRegionName']]+=16
 allowed={m:{cr_slr[cr] for cr in rs} for m,rs in regions.items()}
 expected=C(kinds.values())
 assert len(types)==len(members)==856998
 def slr(y):return min(3,max(0,int(math.floor((y+0.5)/240))))
 def summarize(it,final=False):
  counts={k:{'slr':C(),'y10':C(),'x5':C(),'boundary_bands':C()} for k in ['FF','LUT','other']}
  mods={m:{'slr':C(),'cr':C(),'outside_cr':0,'outside_slr':0,'cells':0} for m in regions}
  seen=set();unknown=0;locs={}
  for name,x,y,cr,s in it:
   if name not in kinds:unknown+=1;continue
   assert name not in seen,name
   seen.add(name);k=kinds[name];c=counts[k];c['slr'][s]+=1
   c['y10'][int(math.floor(y/10))]+=1;c['x5'][int(math.floor(x/5))]+=1
   if abs(x-74.75)<=5:c['boundary_bands']['hpio_x_pm5']+=1
   if min(abs(y-z) for z in [239.5,479.5,719.5])<=10:c['boundary_bands']['any_slr_y_pm10']+=1
   for j,z in enumerate([239.5,479.5,719.5]):
    if abs(y-z)<=10:c['boundary_bands']['slr'+str(j)+'_'+str(j+1)+'_y_pm10']+=1
   if abs(x-74.75)<=5 or min(abs(y-z) for z in [239.5,479.5,719.5])<=10:
    c['boundary_bands']['either']+=1
   m=members[name];ms=mods[m];ms['cells']+=1;ms['slr'][s]+=1
   ms['outside_slr']+=s not in allowed[m]
   if final:
    ms['cr'][cr]+=1;ms['outside_cr']+=cr not in regions[m]
   locs[name]=(s,x,y)
  return {'matched_real_cells':len(seen),'virtual_or_other_dump_rows_excluded':unknown,'classes':counts,'modules':mods},locs
 results=[];inputs=[]
 for run in runs:
  rf=run/'placement/requested.tsv';inputs.append(rf)
  def requests():
   with rf.open() as f:
    for l in f:
     n,target=l.rstrip('\n').split('\t');x,y,cr,s=sites[target.split('/')[0]]
     yield n,x,y,cr,s
  final,floc=summarize(requests(),True)
  assert final['matched_real_cells']==len(types)
  for k,c in final['classes'].items():assert sum(c['slr'].values())==expected[k]
  stages=[]
  for p in sorted((run/'placement').glob('FinalLUTFF-*.gz')):
   def coords():
    with gzip.open(p,'rt') as f:
     for l in f:
      t=l.split(None,2)
      if len(t)<3:continue
      x,y=float(t[0]),float(t[1]);yield t[2].rstrip(),x,y,None,slr(y)
   s,loc=summarize(coords());s['file']=str(p);inputs.append(p)
   assert sum(s['classes']['FF']['slr'].values())==expected['FF']
   s['later_crossed_slr_by_class']={k:sum(v[0]!=floc[n][0] for n,v in loc.items() if kinds[n]==k) for k in ['FF','LUT']}
   stages.append(s)
  results.append({'run':str(run),'initial':json.loads((run/'reports/external_floorplan.json').read_text()),'final_requested':final,'global_stage_snapshots':stages})
 output={'schema':'floorplan-spatial-audit-v1','scope':'AMF requested/imported placement, not routed data; intermediate coordinates from GlobalPlacement_CLBElements end snapshots; only real input-cell names counted. FF=FD*; LUT=LUT1..6. SLR from exact site metadata for final, y seam thresholds for continuous snapshots. Boundary bands are diagnostic windows, not hardware area definitions. CR capacities count 16 FF BELs per unprohibited slice; packing/control sets may reduce usable capacity.',
 'class_totals':dict(expected),'preferred_crs':{m:sorted(rs) for m,rs in regions.items()},'ff_bel_capacity_by_cr':dict(capacities),'runs':results,
 'input_sha256':{str(p):digest(p) for p in inputs}}
 out.parent.mkdir(parents=True,exist_ok=True)
 assert not out.exists()
 out.write_text(json.dumps(output,indent=2)+'\n')
 print('class_totals',dict(expected))
 for r in results:
  f=r['final_requested']
  print(r['run'], 'outside_cr',sum(m['outside_cr'] for m in f['modules'].values()),'outside_slr',sum(m['outside_slr'] for m in f['modules'].values()))
  print('FF final',f['classes']['FF']);print('S01 final',f['modules']['S01'])
  for s in r['global_stage_snapshots']:
   print(Path(s['file']).name,{k:v['slr'] for k,v in s['classes'].items()},'FF bands',s['classes']['FF']['boundary_bands'],'later_slr_moves',s['later_crossed_slr_by_class'])
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();main(a.root,a.output)

