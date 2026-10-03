#!/usr/bin/env python3
"""Review a coordinate-version refit using fixed measured connections and frozen C++.

The original design weights are retained after rebinning. No new placement,
routing, coefficient refit, or selection using external validation occurs here.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import numpy as np
import fit_piecewise_delay as f
from audit_piecewise_fit import shape


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as inp:
        for block in iter(lambda: inp.read(1024*1024), b''): h.update(block)
    return h.hexdigest()


def output(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def estimates(rows, coefficients):
    x = f.matrix(rows)
    return np.maximum(.05, np.array([x[i] @ coefficients[r['band']] for i,r in enumerate(rows)]))


def metrics(rows, prediction):
    if not rows: return dict(n=0)
    truth = np.array([r['target'] for r in rows])
    error = prediction-truth
    w = np.array([r['weight'] for r in rows]); w /= w.sum()
    return dict(n=len(rows), bias_ns=float(error.mean()), mae_ns=float(abs(error).mean()),
        rmse_ns=float(np.sqrt(np.mean(error**2))), p90_abs_ns=float(np.quantile(abs(error),.9)),
        p95_abs_ns=float(np.quantile(abs(error),.95)),
        pearson=float(np.corrcoef(prediction,truth)[0,1]) if np.std(prediction)>1e-12 and np.std(truth)>1e-12 else None,
        weighted_bias_ns=float(w@error), weighted_mae_ns=float(w@abs(error)),
        weighted_rmse_ns=float(np.sqrt(w@(error**2))), effective_sample_size=float(1/(w@w)))


def convex_hull(points):
    points=sorted(set(points))
    def cross(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    def chain(seq):
        h=[]
        for p in seq:
            while len(h)>1 and cross(h[-2],h[-1],p)<=0: h.pop()
            h.append(p)
        return h
    return chain(points)[:-1]+chain(reversed(points))[:-1]


def inside_hull(point, hull):
    x,y=point
    return all((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])>=-1e-9
               for a,b in zip(hull,hull[1:]+hull[:1]))


def paired_comparison(rows, old, new):
    rng=np.random.default_rng(67431)
    y=np.array([r['target'] for r in rows]);w=np.array([r['weight'] for r in rows])
    eo=old-y;en=new-y;changes=[]
    for _ in range(2000):
        ix=rng.integers(0,len(rows),len(rows));ws=w[ix]/w[ix].sum()
        changes.append([np.mean(abs(en[ix]))-np.mean(abs(eo[ix])),
            np.sqrt(np.mean(en[ix]**2))-np.sqrt(np.mean(eo[ix]**2)),
            ws@(abs(en[ix])-abs(eo[ix])),np.sqrt(ws@(en[ix]**2))-np.sqrt(ws@(eo[ix]**2))])
    return dict(replicates=2000,unit='one driver',seed=67431,
        delta='tile recommended minus RPM previous local candidate, same measurements',
        interval95_ns={name:np.quantile(np.array(changes)[:,i],[.025,.975]).tolist()
            for i,name in enumerate(('mae','rmse','weighted_mae','weighted_rmse'))},
        limitation='Conditional on chosen coefficients and this reused validation sample; not design-to-design uncertainty')


def extract_method(text):
    start = text.index('inline float getDelayByModel_conservative(')
    opening = text.index('{', start); depth = 1; stop = opening+1
    while depth:
        depth += (text[stop]=='{') - (text[stop]=='}'); stop += 1
    return text[start:stop]


def native_check(directory, header, coefficients, all_rows):
    text = header.read_text()
    body = extract_method(text)
    arrays = re.findall(r'const float timingC[012]\[10\]\s*=\s*\{[^}]+\};', text)
    assert len(arrays)==3, 'Frozen native coefficient declarations changed'
    raw_coefficients = [[float(v) for v in a.split('{')[1].split('}')[0].split(',') if v.strip()] for a in arrays]
    assert np.max(abs(np.array(raw_coefficients)/1000-f.OLD))<1e-12
    prologue = '''#include <cmath>
#include <iostream>
#include <iomanip>
struct Physical {float penalty(float,float,float,float,float){return 0;}};
struct Device {Physical p; void getClockRegionByLocation(float,float,int &x,int &y){x=y=0;}
bool isPhysicalBoundaryTimingEnabled(){return true;} Physical* getPhysicalBoundaryModel(){return &p;}
int getSLRBoundaryCount(int,int){return 0;}};
struct Model {Device d; Device* deviceInfo=&d; float slrBoundaryDelayNs=1.5;
'''
    epilogue = '''\n};
int main(){Model m;float x,y;std::cout<<std::setprecision(10);
while(std::cin>>x>>y)std::cout<<m.getDelayByModel_conservative(0,0,x,y)<<"\\n";}
'''
    boundary = []
    for radius in (3,6):
        for delta in (-.001,0,.001):
            for angle in (0,np.pi/6,np.pi/4,np.pi/3,np.pi/2):
                dx,dy=(radius+delta)*np.cos(angle),(radius+delta)*np.sin(angle)
                # Avoid float rounding deciding an exactly radial threshold differently.
                dx,dy=float(np.float32(dx)),float(np.float32(dy))
                r2=float(np.float32(np.float32(dx*dx)+np.float32(dy*dy)))
                boundary.append(dict(dx=dx,dy=dy,band=0 if r2<9 else 1 if r2<36 else 2))
    rows = all_rows + boundary
    input_text = ''.join(f"{r['dx']:.12g} {r['dy']:.12g}\n" for r in rows)
    result = dict(frozen_header=str(header), frozen_header_sha256=digest(header),
        extracted_method_sha256=hashlib.sha256(body.encode()).hexdigest(),
        sample_points=len(all_rows), threshold_points=len(boundary),
        scope='Frozen local-distance method; physical penalty mocked zero, appropriate only for no-crossing calibration samples',
        compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0], versions={})
    for name,coefs in [('defaults',f.OLD),('recommended',coefficients)]:
        declarations='\n'.join('const float timingC%d[10] = {%s};' % (i, ','.join(format(v*1000,'.17g') for v in c)) for i,c in enumerate(coefs))
        path=directory/('native-'+name+'.cc');path.write_text(prologue+declarations+'\n'+body+epilogue)
        binary=path.with_suffix('')
        subprocess.run(['g++','-std=c++11','-O2',str(path),'-o',str(binary)],check=True)
        native=np.array([float(v) for v in subprocess.check_output([str(binary)],input=input_text,text=True).splitlines()])
        predicted=estimates(rows,coefs); differences=abs(native-predicted)
        assert len(native)==len(rows) and np.max(differences)<3e-6, (name,float(np.max(differences)))
        result['versions'][name]=dict(max_abs_difference_ns=float(differences.max()),
            source_sha256=digest(path), binary_sha256=digest(binary))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('directory','old-model','model','coordinates','old-candidate','frozen-header','output-candidate'):
        p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();d=a.directory
    report=json.loads((d/'fit.json').read_text());audit=json.loads((d/'fit-audit.json').read_text())
    old_sites,old_cuts=f.load_model(a.old_model);sites,cuts=f.load_model(a.model)
    mapping=json.loads(a.coordinates.read_text())
    assert mapping['model']=='tile-columns-subsites-v3'
    candidate=np.array([report['bands'][b]['coefficients_ps'] for b in f.BANDS])/1000
    recommended=f.OLD.copy();decisions={}
    for i,b in enumerate(f.BANDS):
        item=report['bands'][b];paired=audit['bands'][b]['external_paired']
        passed=item['decision'].startswith('validated') and paired['rmse_delta_95_interval_ns'][1]<0
        if passed: recommended[i]=candidate[i]
        decisions[b]=dict(use_candidate=passed,reason='local validation and paired RMSE interval pass' if passed else 'retain defaults: candidate not supported by validation',
            external_p95_increased=item['external']['candidate']['p95_abs_ns']>item['external']['old']['p95_abs_ns'])
    previous_candidate=np.array([json.loads(a.old_candidate.read_text())['recommended_local_candidate_coefficients_ps'][b] for b in f.BANDS])/1000
    old_data=[];data=[];characteristics=[]
    # Clock-region identity is read from actual site records, not coordinate bins.
    sources=json.loads(a.model.with_name('boundaries.json').read_text())['sources']
    with Path(sources['structure_sites.tsv']['path']).open() as stream:
        cr={r['site']:r['clock_region'] for r in csv.DictReader(stream,delimiter='\t') if r['site'] in sites}
    def key(r):return r['source_pin'],r['sink_pin']
    for layout in (0,1):
        path=d/f'layout-{layout}/samples.tsv'
        old=f.load(path,old_sites,old_cuts);new=f.load(path,sites,cuts)
        original=list(csv.DictReader(path.open(),delimiter='\t'))
        assert len(new)==len(old)==len(original), 'Coordinate update changed endpoint boundary inclusion'
        assert len({r['source_pin'] for r in new})==len(new), 'Bootstrap unit is no longer one observation/driver'
        assert all(1<=int(r['fanout'])<=8 and r['source_type'].startswith(('LUT','FD')) and r['sink_type'].startswith(('LUT','FD')) for r in new)
        assert [key(r) for r in old]==[key(r) for r in new]
        old_data.append(old);data.append(new)
        transitions=Counter(f.BANDS[x['band']]+'->'+f.BANDS[y['band']] for x,y in zip(old,new))
        groups={}
        for band in range(3):
            group=[r for r in new if r['band']==band]
            train=[r for r in group if r['split']!=0]
            design=f.matrix(train);weights=np.array([r['weight'] for r in train])
            groups[f.BANDS[band]]=dict(n=len(group),fit_tune_n=len(train),rank=int(np.linalg.matrix_rank(design)),
                weighted_effective_fit_tune_n=float(weights.sum()**2/(weights@weights)),
                unique_dx_dy=len({(r['dx'],r['dy']) for r in train}),
                dx_range=[min(r['dx'] for r in group),max(r['dx'] for r in group)],
                dy_range=[min(r['dy'] for r in group),max(r['dy'] for r in group)],
                same_cr=sum(cr[r['source_site']]==cr[r['sink_site']] for r in group),
                cross_cr=sum(cr[r['source_site']]!=cr[r['sink_site']] for r in group),
                slrs=dict(Counter(sites[r['source_site']][2] for r in group)))
        characteristics.append(dict(layout=layout,n=len(new),transitions=dict(transitions),bands=groups,
            weights='Retain original RPM-stratum population/sample_count after rebinning; conditional weighted subset statistics'))
    trained={r['source_pin'] for r in data[0] if r['split']!=0}
    selections={'R08_holdout':[i for i,r in enumerate(data[0]) if r['split']==0],
                'R06_unseen_driver':[i for i,r in enumerate(data[1]) if r['source_pin'] not in trained]}
    comparison={};paired={};support={}
    for layout,(name,indices) in enumerate(selections.items()):
        new=[data[layout][i] for i in indices];old=[old_data[layout][i] for i in indices]
        assert not ({r['source_pin'] for r in new}&trained)
        preds={'RPM_defaults':estimates(old,f.OLD),'tile_defaults':estimates(new,f.OLD),
               'RPM_previous_local_candidate':estimates(old,previous_candidate),'tile_recommended_local_candidate':estimates(new,recommended)}
        masks={'overall':list(range(len(new)))}
        masks.update({b:[i for i,r in enumerate(new) if r['band']==band] for band,b in enumerate(f.BANDS)})
        masks.update({label:[i for i,r in enumerate(new) if (cr[r['source_site']]==cr[r['sink_site']])==same] for label,same in [('same_CR',True),('cross_CR',False)]})
        for slr in sorted({sites[r['source_site']][2] for r in new}):
            masks['SLR'+str(slr)]=[i for i,r in enumerate(new) if sites[r['source_site']][2]==slr]
        comparison[name]={group:{k:metrics([new[i] for i in ix],v[ix]) for k,v in preds.items()} for group,ix in masks.items()}
        paired[name]=paired_comparison(new,preds['RPM_previous_local_candidate'],preds['tile_recommended_local_candidate'])
        if layout==1:
            for band,b in enumerate(f.BANDS):
                training=[r for r in data[0] if r['band']==band and r['split']!=0]
                hull=convex_hull((r['dx'],r['dy']) for r in training)
                assert len(hull)>=3
                bounds={axis:[min(r[axis] for r in training),max(r[axis] for r in training)] for axis in ('dx','dy')}
                ix=[i for i,r in enumerate(new) if r['band']==band]
                inside=[i for i in ix if inside_hull((new[i]['dx'],new[i]['dy']),hull)]
                outside=[i for i in ix if i not in inside]
                support[b]=dict(final_fit_extent=bounds,dx_dy_convex_hull=hull,
                    outside_rectangle=sum(any(not bounds[k][0]<=new[i][k]<=bounds[k][1] for k in bounds) for i in ix),
                    external_inside_hull={k:metrics([new[i] for i in inside],v[inside]) for k,v in preds.items() if k.startswith('tile_')},
                    external_outside_hull={k:metrics([new[i] for i in outside],v[outside]) for k,v in preds.items() if k.startswith('tile_')},
                    note='Hull describes sampled distances only; its interior does not establish uniform physical coverage')
    native=native_check(d,a.frozen_header,recommended,data[0]+data[1])
    result=dict(schema='coordinate-delay-review-v1',coordinate_model=mapping['model'],decisions=decisions,
        unique_connection_pairs=len({key(r) for rows in data for r in rows}),
        external_driver_filter=dict(total=len(data[1]),retained=len(selections['R06_unseen_driver']),
            excluded_training_driver=len(data[1])-len(selections['R06_unseen_driver'])),
        sample_characteristics=characteristics,fixed_connection_comparison=comparison,
        previous_vs_current_candidate_paired=paired,distance_support=support,
        recommended_shape=shape(recommended,sites),native_check=native,
        provenance={str(v):digest(v) for v in (a.model,a.old_model,a.coordinates,a.old_candidate,a.frozen_header,d/'fit.json',d/'fit-audit.json')},
        limitations=['Same GETRF design in two routed layouts; earlier validation data reused, not a fresh blind test',
            'Old RPM stratification and original inclusion weights retained; recalculated near/middle/far using new coordinates',
            'No SLR3, cross-SLR, endpoint IO-band crossings, hard-resource endpoints or fanout>8 in fitting data',
            'Endpoint checks do not exclude actual route detours; final cell-site anchors are not saved internal placement iterations',
            'Fits do not establish whole-device applicability; critical-path outside-scope errors worsen with global candidate use'])
    output(d/'coordinate-review.json',result)
    artifact=dict(schema='amf-u250-local-distance-calibration-v2',status='reviewed local candidate; not loaded by placer',
        coordinate_model=mapping['model'],coordinate_map_sha256=digest(a.coordinates),physical_model_sha256=digest(a.model),
        coefficient_unit='ps before /1000',formula=report['formula'],squared_distance_thresholds=[9,36],floor_ns=.05,
        scope=dict(endpoints='LUT/FF ordinary data pins',fanout=[1,8],same_slr=True,endpoint_boundary_crossings=0,observed_slrs=[0,1,2]),
        old_coefficients_ps={b:(f.OLD[i]*1000).tolist() for i,b in enumerate(f.BANDS)},
        fitted_coefficients_ps={b:(candidate[i]*1000).tolist() for i,b in enumerate(f.BANDS)},
        recommended_local_candidate_coefficients_ps={b:(recommended[i]*1000).tolist() for i,b in enumerate(f.BANDS)},
        decision=decisions,training_sample_extents={b:support[b]['final_fit_extent'] for b in f.BANDS},
        training_distance_hulls={b:support[b]['dx_dy_convex_hull'] for b in f.BANDS},
        boundary_penalties='unchanged',defaults_modified=False,new_placement_or_routing_executed=False,
        evidence={name:digest(d/name) for name in ('fit.json','fit-audit.json','coordinate-review.json')})
    output(a.output_candidate,artifact)
    print(json.dumps(dict(decisions=decisions,sample_characteristics=characteristics,
        fixed_external=comparison['R06_unseen_driver']['overall'],native=native),indent=2))


if __name__=='__main__':main()
