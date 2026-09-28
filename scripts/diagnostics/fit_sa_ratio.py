#!/usr/bin/env python3
"""Fit a local-fabric linear distance surrogate for coarse SA, with held-out validation.

This is NOT a replacement for the nonlinear AMF timing model or a timing guarantee.
NumPy is required. Fit only ordinary LUT/FF data edges without endpoint boundary
crossings. The cell-site test cannot exclude a route that detours across a boundary.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
from analyze_boundary_timing_samples import load_model, crossings
from prepare_sa_ratio_samples import digest, family


def split_driver(name):
    return int.from_bytes(hashlib.sha256(('sa-ratio-holdout-v1:'+name).encode()).digest()[:8], 'big') % 5 == 0


def load_samples(path, sites, cuts):
    result=[];dropped=Counter();seen=set()
    with Path(path).open() as f:
        for row in csv.DictReader(f,delimiter='\t'):
            a,b=sites.get(row['source_site']),sites.get(row['sink_site'])
            af,bf=family(row['source_type']),family(row['sink_type'])
            if not a or not b: dropped['missing_site']+=1;continue
            if not af or not bf: dropped['changed_type']+=1;continue
            if a[2]!=b[2] or crossings(a,b,cuts): dropped['boundary']+=1;continue
            dx,dy=abs(a[0]-b[0]),abs(a[1]-b[1])
            if dx+.66*dy<4: dropped['short']+=1;continue
            if row['source_pin'] in seen: raise ValueError('Repeated driver: '+row['source_pin'])
            seen.add(row['source_pin'])
            # REGION identity is SLR + side of the known full-height U250 I/O cut.
            side=sum(c['axis']=='X' and c['kind']=='IO' and a[0]>c['at'] and c['low']<=a[1]<=c['high'] for c in cuts)
            result.append(dict(row,dx=dx,dy=dy,delay=float(row['delay_ns']),fo=int(row['fanout']),
                               group=af+'-'+bf,region=str(a[2])+':'+str(side),slr=a[2],
                               population_weight=float(row['population'])/float(row['sample_count']),
                               holdout=split_driver(row['source_pin'])))
    return result,dict(dropped)


def nuisance_keys(rows):
    return sorted({('group',r['group']) for r in rows}|{('region',r['region']) for r in rows})


def matrix(rows, keys, ratio=None):
    # Full one-hot categories: least squares handles the redundant intercepts.
    # Distance scaling conditions the solver without changing the fitted units.
    geometry=[[r['dx']/40,r['dy']/60] if ratio is None else [(r['dx']+ratio*r['dy'])/40] for r in rows]
    extra=[[np.log2(r['fo'])]+[float(r[k]==v) for k,v in keys] for r in rows]
    return np.hstack((np.asarray(geometry),np.asarray(extra)))


def robust_fit(x,y,weights=None,iterations=30):
    weights=np.ones(len(y)) if weights is None else np.asarray(weights,dtype=float)
    weights=weights/weights.mean();w=weights.copy();beta=np.zeros(x.shape[1])
    for _ in range(iterations):
        root=np.sqrt(w);new=np.linalg.lstsq(x*root[:,None],y*root,rcond=None)[0]
        residual=y-x@new
        scale=max(1e-4,1.4826*np.median(np.abs(residual-np.median(residual))))
        w=weights*np.minimum(1,1.345*scale/np.maximum(np.abs(residual),1e-12))
        if np.max(np.abs(new-beta))<1e-8: beta=new;break
        beta=new
    return beta


def ratio_of(beta):
    if beta[0]<=0 or beta[1]<=0: raise ValueError('Nonpositive directional slope; ratio is not identified')
    return float((beta[1]/60)/(beta[0]/40))


def metrics(rows,prediction):
    residual=np.asarray([r['delay'] for r in rows])-prediction
    w=np.asarray([r['population_weight'] for r in rows])
    return dict(n=len(rows),mae_ns=float(np.mean(np.abs(residual))),rmse_ns=float(np.sqrt(np.mean(residual**2))),
                median_absolute_error_ns=float(np.median(np.abs(residual))),
                population_weighted_mae_ns=float(np.average(np.abs(residual),weights=w)))


def fit_report(rows, bootstrap=0):
    keys=nuisance_keys(rows);x=matrix(rows,keys);y=np.asarray([r['delay'] for r in rows])
    beta=robust_fit(x,y)
    report=dict(n=len(rows),eta=ratio_of(beta),x_ns_per_unit=float(beta[0]/40),y_ns_per_unit=float(beta[1]/60))
    if bootstrap:
        rng=np.random.default_rng(20260928);ratios=[]
        for _ in range(bootstrap):
            idx=rng.integers(0,len(rows),len(rows));b=robust_fit(x[idx],y[idx],iterations=15)
            if b[0]>0 and b[1]>0: ratios.append(ratio_of(b))
        report['bootstrap_driver_95_percent']=list(map(float,np.quantile(ratios,[.025,.975])))
        report['bootstrap_replicates']=len(ratios)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model',type=Path,required=True)
    p.add_argument('--sampling',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--minimum-span',type=float,default=16,help='dx + 0.66*dy; excludes very local edges from coarse SA fit')
    a=p.parse_args();sites,cuts=load_model(a.model)
    layouts=[];dropped=[];sample_files=sorted(a.sampling.glob('layout-*/samples.tsv'))
    if len(sample_files)<2: raise ValueError('Need training and independent layout')
    for path in sample_files:
        rows,gaps=load_samples(path,sites,cuts)
        dropped.append(dict(path=str(path),excluded=gaps,under_fit_span=sum(r['dx']+.66*r['dy']<a.minimum_span for r in rows)))
        layouts.append([r for r in rows if r['dx']+.66*r['dy']>=a.minimum_span])
    train=[r for r in layouts[0] if not r['holdout']]
    holdout=[r for r in layouts[0] if r['holdout']]
    if len(train)<500 or len(holdout)<100: raise ValueError('Insufficient samples')
    keys=nuisance_keys(train);x=matrix(train,keys);y=np.asarray([r['delay'] for r in train])
    beta=robust_fit(x,y);eta=ratio_of(beta);rounded=round(eta,2)
    models={'free_ratio':(None,beta)}
    for label,ratio in [('old_sa',.32),('rounded_candidate',rounded),('global_ratio_reference',.4)]:
        models[label]=(ratio,robust_fit(matrix(train,keys,ratio),y))
    validation={}
    validations=[('heldout_same_layout',holdout)]
    training_drivers={r['source_pin'] for r in train}
    for i,rows in enumerate(layouts[1:],1):
        validations.append((f'independent_layout_{i}',rows))
        unseen=[r for r in rows if r['source_pin'] not in training_drivers]
        if unseen: validations.append((f'independent_layout_{i}_new_drivers',unseen))
    for label,rows in validations:
        validation[label]={name:metrics(rows,matrix(rows,keys,ratio)@b) for name,(ratio,b) in models.items()}
    sensitivity={}
    for i,rows in enumerate(layouts):
        cases={'all':rows,'fanout_1':[r for r in rows if r['fo']==1],
               'span_40_plus':[r for r in rows if r['dx']+.66*r['dy']>=40]}
        for slr in sorted({r['slr'] for r in rows}): cases['slr_'+str(slr)]=[r for r in rows if r['slr']==slr]
        for label,rr in cases.items():
            if len(rr)>=100:
                try: sensitivity[f'layout_{i}_{label}']=fit_report(rr)
                except ValueError as exc: sensitivity[f'layout_{i}_{label}']=dict(n=len(rr),error=str(exc))
    population_beta=robust_fit(x,y,[r['population_weight'] for r in train])
    report=dict(schema='sa-ratio-fit-v1',scope='U250 GETRF local-fabric coarse SA linear distance surrogate',
                analysis_script_sha256=digest(__file__),numpy_version=np.__version__,
                dataset_coverage=[dict(n=len(rr),slr=dict(Counter(str(r['slr']) for r in rr)),
                                       types=dict(Counter(r['group'] for r in rr)),
                                       orientation=dict(Counter(r['stratum'].split(':')[-2] for r in rr))) for rr in layouts],
                minimum_span=a.minimum_span,fit=fit_report(train,150),candidate_sa_y2x_ratio=rounded,
                population_weighted_fit=ratio_of(population_beta),validation=validation,sensitivity=sensitivity,
                excluded=dropped,inputs={str(f):digest(f) for f in [a.model,*sample_files]},
                methodology='Huber IRLS: delay = bx*abs(dx) + by*abs(dy) + type/region intercepts + log2(fanout); eta=by/bx. Driver-hash 80/20 split on layout 0, layout 1 withheld. Balanced strata. 150 driver bootstraps.',
                limitations=['Same GETRF netlist in different layouts, not independent designs.',
                             'Endpoint-site geometry, not actual route path; boundary detours and congestion remain confounders.',
                             'Low-fanout LUT/FF data connections only; not calibrated for dedicated cascades or hard-IP edges.',
                             'Coarse linear distance ratio, not the nonlinear AMF timing model; no SLR/IO coefficients refitted.'])
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('fit','candidate_sa_y2x_ratio','population_weighted_fit','validation','sensitivity')},indent=2))


if __name__=='__main__': main()
