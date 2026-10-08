#!/usr/bin/env python3
"""Analyze existing route/AMF records; compile only a standalone formula probe."""
import argparse, collections, csv, gzip, hashlib, json, math, re, subprocess
from pathlib import Path

def rows(p):
    with p.open() as f:
        return list(csv.DictReader(f, delimiter='\t'))

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--run', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    r, run, out = args.repo, args.run, args.output
    out.mkdir(parents=True, exist_ok=True)
    build = json.loads((run/'inputs/build_manifest.json').read_text())
    src = Path(build['source_snapshot'])
    h = (src/'lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.h').read_text()
    cc = (src/'lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.cc').read_text()
    physical = (src/'lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.cc').read_text()
    c0 = [float(x) for x in re.search(r'timingC0\[10\] = \{([^}]+)',h)[1].split(',')]
    fitted = re.findall(r'\? std::array<float, 10>\{([^}]+)',cc)
    c1,c2 = ([float(x) for x in value.split(',')] for value in fitted)
    cfg = json.loads((run/'config.json').read_text())
    model = Path(cfg['physical boundary model file'])
    sites, cuts = {}, []
    for line in model.open():
        f = line.rstrip().split('\t')
        if f[0]=='SITE': sites[f[1]]=(float(f[3]),float(f[4]),int(f[5]))
        if f[0]=='BOUNDARY':
            cuts.append(dict(kind=f[2],axis=f[3],at=float(f[4]),low=float(f[5]),high=float(f[6]),penalty=float(f[7]),active=f[8]=='1'))
    def crossings(a,b):
        return sum(c['active'] and c['kind']=='SLR' and ((a[1]<=c['at'])!=(b[1]<=c['at'])) for c in cuts)
    def estimate(a,b):
        x,y=abs(a[0]-b[0]),abs(a[1]-b[1]);d=x*x+y*y
        coef=c0 if d<9 else c1 if d<36 else c2;x*=2
        base=max(.05,(coef[0]+x**.3*coef[1]+y**.3*coef[2]+x**.5*coef[3]+y**.5*coef[4])/1000)
        penalty=0.
        for c in cuts:
            aa,bb=(a[0],b[0]) if c['axis']=='X' else (a[1],b[1])
            u,v=(a[1],b[1]) if c['axis']=='X' else (a[0],b[0])
            yes=((aa<=c['at'])!=(bb<=c['at'])) if c['kind']=='SLR' else min(aa,bb)<c['at']<max(aa,bb) and c['low']<=u<=c['high'] and c['low']<=v<=c['high']
            if c['active'] and yes:penalty+=float(cfg['SLRBoundaryDelayNs']) if c['kind']=='SLR' else c['penalty']
        return dict(base_ns=base,penalty_ns=penalty,model_ns=base+penalty,segment='near' if d<9 else 'middle' if d<36 else 'far')
    cons=rows(run/'reports/physical/timing_connections.tsv')
    paths=rows(run/'reports/physical/timing_paths.tsv')
    by=collections.defaultdict(list)
    for row in cons:by[row['path']].append(row)
    path_results=[]
    for path in paths:
        subset=by[path['path']]
        path_results.append(dict(path,slr_crossings=sum(crossings(sites[x['source_site']],sites[x['sink_site']]) for x in subset),connections=len(subset)))
    worst=sorted(by['0'],key=lambda x:int(x['order']))
    for edge in worst:
        a,b=sites[edge['source_site']],sites[edge['sink_site']]
        edge.update(source_xy_slr=a,sink_xy_slr=b,**estimate(a,b))
    names={c[k].rsplit('/',1)[0] for c in worst for k in ('source_pin','sink_pin')}
    grouped={};macro={}
    with gzip.open(run/'placement/PUInfoFinal.gz','rt') as f:
        for line in f:
            if line.startswith('Macro: '):macro={'header':line.strip()}
            elif line.startswith('  macroType:'):macro['type']=line.split()[-1]
            elif line.startswith('  placedAt:'):macro['xy']=list(map(float,line.split()[1:]))
            else:
                m=re.match(r'\s+CellId: (\d+) targetCellTypeEnumId: \d+ name: (\S+) (\S+) (\S+)',line)
                if m and m[2] in names:grouped[m[2]]={'id':int(m[1]),'macro':dict(macro),'offset':[float(m[3]),float(m[4])]}
    assert set(grouped)==names
    snapshots={}
    for path in sorted((run/'placement').glob('FinalLUTFF-*.gz')):
        locations={}
        with gzip.open(path,'rt') as f:
            for line in f:
                vals=line.split()
                if len(vals)==3 and vals[2] in names:locations[vals[2]]=[float(vals[0]),float(vals[1])]
        snapshots[path.name]=locations
    endpoint=paths[0]['endpoint'].rsplit('/',1)[0]
    eid=grouped[endpoint]['id']
    ranks=rows(run/'reports/physical/amf_critical_paths.tsv')
    final_rank=[x for x in ranks if x['stage']=='amf-final-packed' and int(x['endpoint'])==eid]
    boundary=rows(run/'reports/physical/amf_boundaries.tsv')
    # Compile the frozen native delay/crossing function bodies verbatim. Only
    # device plumbing is stubbed: physical mode always enabled, CR indices unused.
    fn=function(h,'inline float getDelayByModel_conservative')
    cross='static '+function(physical,'bool PhysicalBoundaryModel::crosses').replace('PhysicalBoundaryModel::','')
    pen=function(physical,'float PhysicalBoundaryModel::penalty').replace('PhysicalBoundaryModel::','')
    declarations='\n'.join('float timingC%d[10]={%s};'%(i,','.join(map(str,c))) for i,c in enumerate((c0,c1,c2)))
    cpp='#include <cmath>\n#include <vector>\n#include <string>\n#include <iostream>\n#include <iomanip>\n'
    cpp+='struct Physical { struct Boundary {std::string kind; char orientation; float coordinate,low,high,penaltyNs; bool active;}; std::vector<Boundary> boundaries;\n'+cross+'\n'+pen+'\n};\n'
    cpp+='struct Device {Physical physical; void getClockRegionByLocation(float,float,int &x,int &y){x=0;y=0;} bool isPhysicalBoundaryTimingEnabled(){return true;} Physical* getPhysicalBoundaryModel(){return &physical;} int getSLRBoundaryCount(int,int){return 0;} };\n'
    cpp+='struct Probe {Device *deviceInfo; float slrBoundaryDelayNs=1.5f;\n'+declarations+'\n'+fn+'\n};\n'
    cpp+='int main(){Device d; Probe p{&d};'
    for c in cuts:
        cpp+='d.physical.boundaries.push_back({"%s",\'%s\',%sf,%sf,%sf,%sf,%s});'%(c['kind'],c['axis'],c['at'],c['low'],c['high'],c['penalty'],'true' if c['active'] else 'false')
    cpp+='float a,b,c,e;std::cout<<std::setprecision(10);while(std::cin>>a>>b>>c>>e){p.slrBoundaryDelayNs=0;auto off=p.getDelayByModel_conservative(a,b,c,e);p.slrBoundaryDelayNs=1.5f;std::cout<<off<<" "<<p.getDelayByModel_conservative(a,b,c,e)<<"\\n";}}'
    (out/'native_formula_probe.cc').write_text(cpp)
    subprocess.run(['g++','-std=c++14','-O2',str(out/'native_formula_probe.cc'),'-o',str(out/'native_formula_probe')],check=True)
    pairs=''.join('%s %s %s %s\n'%(*x['source_xy_slr'][:2],*x['sink_xy_slr'][:2]) for x in worst)
    (out/'native_pairs.txt').write_text(pairs)
    result=subprocess.run([str(out/'native_formula_probe')],input=pairs,text=True,stdout=subprocess.PIPE,check=True)
    (out/'native_predictions.txt').write_text(result.stdout)
    native=[list(map(float,line.split())) for line in result.stdout.splitlines()]
    error=max(abs(values[1]-edge['model_ns']) for values,edge in zip(native,worst))
    assert len(native)==len(worst) and error<1e-6
    rels=['lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.h','lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.cc','lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.cc','lib/HiFPlacer/placement/packing/ParallelCLBPacker.cc','lib/HiFPlacer/placement/packing/ParallelCLBPacker_PackingCLBCluster.cc','lib/HiFPlacer/placement/legalization/MacroLegalizer.h','app/AMFPlacer/AMFPlacer.h']
    checks={rel:hashlib.sha256((src/rel).read_bytes()).hexdigest()==hashlib.sha256((r/'src'/rel).read_bytes()).hexdigest() for rel in rels}
    assert all(checks.values())
    report={'schema':'amf-critical-slr-diagnosis-v1','run':str(run),'model':str(model),'source_snapshot':str(src),'source_current_matches_frozen':checks,
            'paths':path_results,'negative_crossing_histogram':dict(collections.Counter(x['slr_crossings'] for x in path_results if float(x['slack_ns'])<0)),
            'all_crossing_histogram':dict(collections.Counter(x['slr_crossings'] for x in path_results)),
            'worst_connections':worst,'worst_cells_final_macros':grouped,'global_snapshots':snapshots,
            'same_endpoint_final_amf_rank':final_rank,'amf_final_top3':[x for x in ranks if x['stage']=='amf-final-packed'][:3],
            'boundary_last':boundary[-1],'boundary_before_pack':[x for x in boundary if x['stage']=='amf-before-pack'],
            'native_check':{'pairs':len(native),'max_error_ns':error,'penalty_sum_ns':sum(b-a for a,b in native),'native_model_net_sum_ns':sum(b for a,b in native),
                            'scope':'Frozen production formula/crosses/penalty bodies compiled verbatim; physical model boundary values loaded from run model; unused CR query stubbed. This is not an entire AMF historical STA replay.'},
            'limitations':['200 endpoint-distinct worst setup paths, not all design paths.','NET_DELAY.SLOW_MAX ps/1000 may differ from path arc sum by rounding and transition choice.','Post-route sites reconstruct final geometry, not historical internal pin positions.','Final PU snapshot includes post-packing LCLB groups; CARRY macro structures were retained.']}
    (out/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'paths':len(paths),'hist':report['negative_crossing_histogram'],'native_check':report['native_check'],'amf_rank':final_rank},ensure_ascii=False))
if __name__=='__main__':main()
