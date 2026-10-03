#!/usr/bin/env python3
"""Assess/fix local LUT/FF distance coefficients; no boundary or default changes."""
import argparse, csv, hashlib, json
from pathlib import Path
import numpy as np
from analyze_boundary_timing_samples import load_model, crossings

OLD=np.array([[95.05263521,-26.50563359,77.42394117,106.29195883,-14.975527],
 [123.05017047,-169.25614191,-117.28028144,208.53573639,174.2573465],
 [234.7694101,-433.99467294,-64.96319998,373.78606257,139.45226658]])/1000
BANDS=['near','middle','far']

def bucket(name):
 return int.from_bytes(hashlib.sha256(('piecewise-holdout-v1:'+name).encode()).digest()[:8],'big')%5

def load(path,sites,cuts):
 rows=[];seen=set()
 with Path(path).open() as f:
  for r in csv.DictReader(f,delimiter='\t'):
   key=(r['source_pin'],r['sink_pin'])
   if key in seen:raise ValueError('Duplicate connection')
   seen.add(key);a,b=sites[r['source_site']],sites[r['sink_site']]
   if crossings(a,b,cuts) or a[2]!=b[2]:continue
   dx,dy=abs(a[0]-b[0]),abs(a[1]-b[1]);d=dx*dx+dy*dy
   rows.append(dict(r,dx=dx,dy=dy,band=0 if d<9 else 1 if d<36 else 2,
     target=float(r['delay_ns']),weight=float(r['population'])/float(r['sample_count']),split=bucket(r['source_pin'])))
 return rows

def matrix(rows):
 a=np.array([[r['dx']*2,r['dy']] for r in rows]).reshape(-1,2)
 return np.column_stack([np.ones(len(a)),a[:,0]**.3,a[:,1]**.3,a[:,0]**.5,a[:,1]**.5])

def predict(rows,c):return np.maximum(.05,matrix(rows)@c)

def metrics(rows,c):
 if not rows:return {'n':0}
 y=np.array([r['target'] for r in rows]);p=predict(rows,c);e=p-y;w=np.array([r['weight'] for r in rows]);w/=w.sum()
 return dict(n=len(rows),bias_ns=float(e.mean()),mae_ns=float(abs(e).mean()),rmse_ns=float(np.sqrt((e*e).mean())),
  p90_abs_ns=float(np.quantile(abs(e),.9)),p95_abs_ns=float(np.quantile(abs(e),.95)),
  pearson=float(np.corrcoef(p,y)[0,1]) if np.std(p)>1e-12 and np.std(y)>1e-12 else None,
  population_weighted_bias_ns=float(w@e),population_weighted_mae_ns=float(w@abs(e)),population_weighted_rmse_ns=float(np.sqrt(w@(e*e))))

def fit(rows,prior,alpha):
 x=matrix(rows);y=np.array([r['target'] for r in rows]);w=np.array([r['weight'] for r in rows]);w/=w.mean()
 scale=np.maximum(np.std(x,axis=0),1e-6);scale[0]=1
 z=x/scale
 penalty=np.eye(5)*alpha;penalty[0,0]=alpha*.01
 coef=np.linalg.solve(z.T@(w[:,None]*z)+penalty,z.T@(w*y)+penalty@(prior*scale))/scale
 return coef

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['train','validation','model','output']:p.add_argument('--'+k,type=Path,required=True)
 a=p.parse_args();sites,cuts=load_model(a.model);train=load(a.train,sites,cuts);external=load(a.validation,sites,cuts)
 report={'scope':'LUT/FF, fanout 1-8, no endpoint boundary crossings; post-route site anchors; not saved internal timing-iteration predictions',
 'split':'driver hash buckets 2-4 fit, 1 tuning, 0 locked holdout; final fit 1-4; external validation uses unseen training drivers',
 'formula':'max(0.05,(c0+c1*(2dx)^0.3+c2*dy^0.3+c3*(2dx)^0.5+c4*dy^0.5)/1000); boundary penalties unchanged',
 'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.train,a.validation,a.model]},'bands':{}}
 trained_drivers={r['source_pin'] for r in train if r['split']!=0}
 for band in range(3):
  rows=[r for r in train if r['band']==band];fitrows=[r for r in rows if r['split']>1];tune=[r for r in rows if r['split']==1];test=[r for r in rows if r['split']==0]
  ext=[r for r in external if r['band']==band and r['source_pin'] not in trained_drivers]
  item={'counts':dict(fit=len(fitrows),tune=len(tune),holdout=len(test),external_unseen_driver=len(ext)),'old_coefficients_ps':(OLD[band]*1000).tolist()}
  if min(len(fitrows),len(tune),len(test),len(ext))<25 or np.linalg.matrix_rank(matrix(fitrows))<5:
   item['decision']='insufficient data/rank; retain old parameters';report['bands'][BANDS[band]]=item;continue
  candidates=[]
  for alpha in [.001,.01,.1,1,10,100,1000]:
   c=fit(fitrows,OLD[band],alpha);score=metrics(tune,c)['population_weighted_rmse_ns'];candidates.append((score,alpha))
  _,alpha=min(candidates);c=fit([r for r in rows if r['split']!=0],OLD[band],alpha)
  item.update(ridge_alpha=alpha,coefficients_ps=(c*1000).tolist(),design_condition_number=float(np.linalg.cond(matrix(fitrows))),
   holdout={'old':metrics(test,OLD[band]),'candidate':metrics(test,c)},external={'old':metrics(ext,OLD[band]),'candidate':metrics(ext,c)})
  passes=all(item[k]['candidate']['population_weighted_rmse_ns']<item[k]['old']['population_weighted_rmse_ns'] and item[k]['candidate']['mae_ns']<item[k]['old']['mae_ns'] for k in ['holdout','external'])
  item['decision']='validated local candidate; not global default' if passes else 'validation fails; retain old parameters'
  item['sample_extent']={k:[min(r[k] for r in rows),max(r[k] for r in rows)] for k in ['dx','dy']}
  report['bands'][BANDS[band]]=item
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
