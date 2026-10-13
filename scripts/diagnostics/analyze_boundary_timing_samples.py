#!/usr/bin/env python3
"""Cell-site crossing proxies and routed net-delay samples; never SLL usage."""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import zipfile

def rows(path):
    with Path(path).open() as f:yield from csv.DictReader(f,delimiter='\t')

def load_model(path):
    sites={};cuts=[]
    for line in Path(path).read_text().splitlines():
        f=line.split('\t')
        if f[0]=='SITE':sites[f[1]]=(float(f[3]),float(f[4]),int(f[5]))
        elif f[0]=='BOUNDARY' and f[8]=='1':cuts.append(dict(kind=f[2],axis=f[3],at=float(f[4]),low=float(f[5]),high=float(f[6]),penalty=float(f[7])))
    return sites,cuts

def crossings(a,b,cuts):
    counts=Counter()
    for c in cuts:
        if c['axis']=='Y':
            crossed=(a[1]<=c['at'])!=(b[1]<=c['at'])
        else:
            crossed=(min(a[0],b[0])<c['at']<max(a[0],b[0]) and c['low']<=a[1]<=c['high'] and c['low']<=b[1]<=c['high'])
        if crossed:counts[c['kind']]+=1
    return counts

def distance_delay(a,b):
    x=abs(a[0]-b[0]);y=abs(a[1]-b[1]);d=x*x+y*y
    cs=(95.05263521,-26.50563359,77.42394117,106.29195883,-14.975527) if d<9 else ((123.05017047,-169.25614191,-117.28028144,208.53573639,174.2573465) if d<36 else (234.7694101,-433.99467294,-64.96319998,373.78606257,139.45226658))
    x*=2
    return max(.05,(cs[0]+x**.3*cs[1]+y**.3*cs[2]+x**.5*cs[3]+y**.5*cs[4])/1000)

def stats(values):
    return dict(n=len(values),median=statistics.median(values),mean=statistics.mean(values),min=min(values),max=max(values)) if values else dict(n=0)

def analyze(model,directory,slr_penalty=1.5):
    sites,cuts=load_model(model);slr_y={sid:min(p[1] for p in sites.values() if p[2]==sid) for sid in {p[2] for p in sites.values()}}
    slr_rank={sid:i for i,sid in enumerate(sorted(slr_y,key=slr_y.get))}
    groups=defaultdict(list);unique={};paths=defaultdict(Counter);path_slrs=defaultdict(list);missing=0
    for row in rows(directory/'timing_connections.tsv'):
        a=sites.get(row['source_site']);b=sites.get(row['sink_site'])
        if a is None or b is None:missing+=1;continue
        c=crossings(a,b,cuts);pid=row['path'];paths[pid].update(c)
        path_slrs[pid].extend((a[2],b[2]))
        key=(row['source_pin'],row['sink_pin'])
        if key in unique:continue
        distance=abs(a[0]-b[0])+.4*abs(a[1]-b[1]);fanout=int(row['fanout'])
        band='0-10' if distance<10 else '10-50' if distance<50 else '50-150' if distance<150 else '150+'
        fan='1' if fanout==1 else '2-8' if fanout<=8 else '9-64' if fanout<=64 else '65+'
        base=distance_delay(a,b);penalty=sum((slr_penalty if cut['kind']=='SLR' else cut['penalty']) for cut in cuts if crossings(a,b,[cut]))
        measured=float(row['routed_delay_ns']);residual=measured-base
        group=(band,fan,row['source_type'],row['sink_type'],c['SLR'],c['IO'])
        groups[group].append(residual)
        unique[key]=dict(row,slr_crossings=c['SLR'],io_crossings=c['IO'],distance_model_ns=base,boundary_model_ns=base+penalty,residual_ns=residual)
    path_report=[]
    for row in rows(directory/'timing_paths.tsv'):
        seq=path_slrs[row['path']];compressed=[x for i,x in enumerate(seq) if i==0 or seq[i-1]!=x]
        # Returns to a previously visited SLR, distinct from crossing count.
        returns=len(compressed)-len(set(compressed))
        direct=abs(slr_rank[compressed[-1]]-slr_rank[compressed[0]]) if compressed else 0
        roundtrips=max(0,paths[row['path']]['SLR']-direct)//2
        path_report.append(dict(row,**dict(paths[row['path']]),slr_sequence=compressed,slr_returns=returns,slr_roundtrips=roundtrips))
    report=dict(schema='boundary-timing-samples-v1',scope='constrained-path sample; geometry uses cell sites, not pin offsets or routed SLL usage',
        model_sha256=hashlib.sha256(Path(model).read_bytes()).hexdigest(),unique_driver_sink_samples=len(unique),missing_site_rows=missing,
        sampling_gaps=sum(1 for _ in rows(directory/'timing_sample_gaps.tsv')),slr_penalty_ns=slr_penalty,
        roundtrip_definition='(cumulative SLR seams - start/end direct seams)/2; geometric excess crossing pairs',
        conclusion='Coefficients remain heuristic. Groups are observational and confounded by distance, load and routing; no automatic calibration.',
        groups=[dict(distance=key[0],fanout=key[1],source_type=key[2],sink_type=key[3],slr_crossings=key[4],io_crossings=key[5],residual_ns=stats(v)) for key,v in sorted(groups.items())],paths=path_report)
    (directory/'timing_sample_analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    if unique:
        with (directory/'unique_timing_samples.tsv').open('w') as f:
            w=csv.DictWriter(f,fieldnames=list(next(iter(unique.values()))),delimiter='\t');w.writeheader();w.writerows(unique.values())
    return report

def audit_placements(run,model,netlist,clock_file=None):
    sites,cuts=load_model(model)
    clocks=set(Path(clock_file).read_text().split()) if clock_file else set()
    stages={};locations={}
    for stage in ('imported','placed','routed'):
        path=run/'placement'/f'{stage}_cell_sites.tsv'
        if path.exists():locations[stage]={r['cell']:sites[r['site']] for r in rows(path) if r['site'] in sites};stages[stage]=Counter()
    constants=set();cell=None;excluded=Counter()
    with zipfile.ZipFile(netlist) as z,z.open(z.namelist()[0]) as f:
        for raw in f:
            if raw.startswith(b'curCell=> '):
                fields=raw.decode().split()
                if fields[3] in ('VCC','GND'):constants.add(fields[1])
    with zipfile.ZipFile(netlist) as z,z.open(z.namelist()[0]) as f:
        for raw in f:
            line=raw.decode().split()
            if not line:continue
            if line[0]=='curCell=>':
                cell=line[1]
                if line[3] in ('VCC','GND'):constants.add(cell)
            elif line[0]=='pin=>' and 'dir=>' in line and line[line.index('dir=>')+1]=='IN' and 'drivepin=>' in line:
                d=line[line.index('drivepin=>')+1:];net=line[line.index('net=>')+1]
                if not d or '/' not in d[0]:continue
                # AMF's clock file identifies nets by their driver pin, e.g.
                # @PORT/ap_clk; the exported logical net name can be n568507.
                if d[0] in clocks or net in clocks:
                    excluded['clock_edges']+=1;continue
                source=d[0].rsplit('/',1)[0]
                if source in constants:
                    excluded['constant_edges']+=1;continue
                if d[0].startswith('@PORT/'):
                    excluded['external_port_edges_without_fabric_site']+=1;continue
                for stage,loc in locations.items():
                    a=loc.get(source);b=loc.get(cell);c=stages[stage]
                    if a is None or b is None:c['missing_site_edges']+=1;continue
                    c['driver_sink_edges']+=1;cross=crossings(a,b,cuts);c['slr_crossings']+=cross['SLR'];c['io_crossings']+=cross['IO'];c['crossing_edges']+=bool(cross)
    result=dict(schema='backend-boundaries-v2',
        scope='unique data driver-sink pins at actual fabric cell sites; listed clocks, constants and external ports without fabric sites counted separately; geometric crossings, not SLL usage',
        analysis_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        model_sha256=hashlib.sha256(Path(model).read_bytes()).hexdigest(),
        excluded_input_edges=dict(excluded),stages={k:dict(v) for k,v in stages.items()})
    (run/'reports/physical/backend_boundaries.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--model',required=True,type=Path);p.add_argument('--directory',required=True,type=Path);p.add_argument('--slr-penalty',type=float,default=1.5)
    a=p.parse_args();print(json.dumps({k:v for k,v in analyze(a.model,a.directory,a.slr_penalty).items() if k not in ('groups','paths')}))
