#!/usr/bin/env python3
"""Audit locked validation errors, shape, coefficient stability, and transfer scope."""
import argparse,csv,hashlib,json
from pathlib import Path
import numpy as np
import fit_piecewise_delay as f
from analyze_boundary_timing_samples import crossings

def paired_interval(rows,old,new,rng):
 y=np.array([r['target'] for r in rows]);eo=f.predict(rows,old)-y;en=f.predict(rows,new)-y
 delta=[]
 for _ in range(1000):
  ix=rng.integers(0,len(rows),len(rows));delta.append([np.mean(abs(en[ix]))-np.mean(abs(eo[ix])),np.sqrt(np.mean(en[ix]**2))-np.sqrt(np.mean(eo[ix]**2))])
 a=np.array(delta)
 return {'bootstrap_unit':'driver (one observation per driver)','replicates':1000,'mae_delta_95_interval_ns':np.quantile(a[:,0],[.025,.975]).tolist(),'rmse_delta_95_interval_ns':np.quantile(a[:,1],[.025,.975]).tolist()}

def raw(dx,dy,c):return c[0]+c[1]*(2*dx)**.3+c[2]*dy**.3+c[3]*(2*dx)**.5+c[4]*dy**.5

def shape(coefs,sites):
 widths=[max(p[k] for p in sites.values())-min(p[k] for p in sites.values()) for k in [0,1]]
 grids=[(np.linspace(0,3,121),np.linspace(0,3,121)),(np.linspace(0,6,121),np.linspace(0,6,121)),(np.linspace(0,widths[0],201),np.linspace(0,widths[1],201))]
 report={}
 for i,(gx,gy) in enumerate(grids):
  x,y=np.meshgrid(gx,gy);d=x*x+y*y;mask=(d<9) if i==0 else ((d>=9)&(d<36)) if i==1 else d>=36
  v=raw(x[mask],y[mask],coefs[i]);vx=raw(x[mask]+1e-4,y[mask],coefs[i]);vy=raw(x[mask],y[mask]+1e-4,coefs[i])
  report[f.BANDS[i]]=dict(grid_points=len(v),raw_min_ns=float(v.min()),raw_max_ns=float(v.max()),floor_fraction=float(np.mean(v<.05)),x_decreasing_fraction=float(np.mean(vx<v-1e-8)),y_decreasing_fraction=float(np.mean(vy<v-1e-8)))
 jumps={}
 for i,r in enumerate([3,6]):
  theta=np.linspace(0,np.pi/2,361);x=r*np.cos(theta);y=r*np.sin(theta);v=np.maximum(.05,raw(x,y,coefs[i+1]))-np.maximum(.05,raw(x,y,coefs[i]))
  jumps[str(r)]={'minimum_jump_ns':float(v.min()),'maximum_jump_ns':float(v.max()),'max_abs_jump_ns':float(abs(v).max())}
 return dict(device_extent=widths,bands=report,radial_boundary_jumps=jumps)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['directory','model']:p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--critical-run',type=Path,action='append',default=[])
 a=p.parse_args();d=a.directory;fit=json.loads((d/'fit.json').read_text());sites,cuts=f.load_model(a.model);tr=f.load(d/'layout-0/samples.tsv',sites,cuts);ext=f.load(d/'layout-1/samples.tsv',sites,cuts)
 coefs=np.array([fit['bands'][b]['coefficients_ps'] for b in f.BANDS])/1000
 trained={r['source_pin'] for r in tr if r['split']!=0};rng=np.random.default_rng(39519)
 result={'bands':{},'old_shape':shape(f.OLD,sites),'candidate_shape':shape(coefs,sites),'critical_transfer':{},'bootstrap_seed':39519}
 for i,b in enumerate(f.BANDS):
  r=[r for r in tr if r['band']==i and r['split']!=0];e=[r for r in ext if r['band']==i and r['source_pin'] not in trained]
  cs=[];pm=[]
  for _ in range(200):
   sub=[r[j] for j in rng.integers(0,len(r),len(r))];c=f.fit(sub,f.OLD[i],fit['bands'][b]['ridge_alpha']);cs.append(c*1000);pm.append(f.predict(e,c))
  ci=np.quantile(cs,[.025,.975],axis=0);pi=np.quantile(pm,[.025,.975],axis=0)
  groups={}
  for groupkey in ['fanout','source_type','sink_type']:
   groups[groupkey]={key:{'old':f.metrics([r for r in e if r[groupkey]==key],f.OLD[i]),'candidate':f.metrics([r for r in e if r[groupkey]==key],coefs[i])} for key in sorted({r[groupkey] for r in e})}
  below=sum(r['target']<.05 for r in e)
  result['bands'][b]=dict(external_paired=paired_interval(e,f.OLD[i],coefs[i],rng),coefficient_bootstrap_95_ps=ci.tolist(),coefficient_bootstrap_replicates=200,external_prediction_interval_width_median_ns=float(np.median(pi[1]-pi[0])),external_prediction_interval_width_p95_ns=float(np.quantile(pi[1]-pi[0],.95)),below_floor_samples=below,external_groups=groups)
 for run in a.critical_run:
  groups={}
  with (run/'reports/physical/unique_timing_samples.tsv').open() as inp:
   for row in csv.DictReader(inp,delimiter='\t'):
    ap,bp=sites[row['source_site']],sites[row['sink_site']];dx,dy=abs(ap[0]-bp[0]),abs(ap[1]-bp[1]);r2=dx*dx+dy*dy;i=0 if r2<9 else 1 if r2<36 else 2
    c=crossings(ap,bp,cuts);penalty=sum((1.5 if cut['kind']=='SLR' else cut['penalty']) for cut in cuts if crossings(ap,bp,[cut]))
    local=not c and int(row['fanout'])<=8 and (row['source_type'].startswith(('LUT','FD'))) and (row['sink_type'].startswith(('LUT','FD')))
    key=f.BANDS[i]+(' local-lowfo' if local else 'outside-fit-scope')
    true=float(row['routed_delay_ns']);old=max(.05,raw(dx,dy,f.OLD[i]))+penalty;new=max(.05,raw(dx,dy,coefs[i]))+penalty
    groups.setdefault(key,[]).append((old-true,new-true))
  result['critical_transfer'][run.name]={k:{'n':len(v),'old_mae_ns':float(np.mean(abs(np.array(v)[:,0]))),'counterfactual_all_candidate_mae_ns':float(np.mean(abs(np.array(v)[:,1]))),'old_bias_ns':float(np.mean(np.array(v)[:,0])),'counterfactual_all_candidate_bias_ns':float(np.mean(np.array(v)[:,1]))} for k,v in groups.items()}
 result['scope']='Bootstrap conditional on selected ridge and sampled driver distribution; same design, two layouts. Full-device grids are extrapolation diagnostics, not measured accuracy.'
 (d/'fit-audit.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
 print(json.dumps({k:v for k,v in result.items() if k!='bands'},indent=2))
 print(json.dumps({k:{x:y for x,y in v.items() if x!='external_groups'} for k,v in result['bands'].items()},indent=2))
if __name__=='__main__':main()
