#!/usr/bin/env python3
"""Compare fixed AMF assignments against netlist connectivity. No implementation is run."""
import collections, csv, hashlib, json, re, zipfile
from pathlib import Path

def digest(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''): h.update(b)
    return h.hexdigest()

def main(root):
    root=Path(root); ev=root/'experiments/evidence/20261007-paper-boundaries-full-01'
    idx=json.loads((ev/'launch.json').read_text())
    runs=[Path(idx['baseline_run']),Path(idx['run_directory'])]
    cfg=json.loads((runs[1]/'config.json').read_text())
    sites={}
    with zipfile.ZipFile(cfg['vivado extracted device information file']) as z:
        with z.open(z.namelist()[0]) as f:
            for raw in f:
                fields=dict(re.findall(r'(\w+)=>\s+(\S+)',raw.decode()))
                sites[fields['site']]=int(fields['slr'])
    placements=[]
    for run in runs:
        d={}
        with (run/'placement/requested.tsv').open() as f:
            for l in f:
                name,target=l.rstrip('\n').split('\t')
                assert name not in d
                d[name]=sites[target.split('/')[0]]
        placements.append(d)
    assert placements[0].keys()==placements[1].keys()
    members={}
    with Path(cfg['external floorplan membership file']).open() as f:
        members={r['cell']:r['module'] for r in csv.DictReader(f,delimiter='\t')}
    stats=[dict(edges=collections.Counter(),nets=[set() for _ in range(3)],pairs=collections.Counter(),types=collections.Counter(),skipped=collections.Counter()) for _ in runs]
    nt=0; pin_edges=0
    with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
        with z.open(z.namelist()[0]) as f:
            for raw in f:
                tok=raw.decode().split()
                if not tok: continue
                if tok[0]=='curCell=>':
                    cell,kind=tok[1],tok[3];nt+=1
                    for d,s in zip(placements,stats):
                        if cell in d:s['types'][kind+'@SLR'+str(d[cell])]+=1
                    continue
                if tok[0]!='pin=>' or tok[5]!='IN': continue
                assert tok[2]=='refpin=>' and tok[4]=='dir=>' and tok[6]=='net=>' and tok[8]=='drivepin=>'
                net=tok[7];driver=tok[9] if len(tok)>9 else ''
                if driver.startswith('@PORT/') or driver.startswith('<') or '/' not in driver:
                    for s in stats:s['skipped']['port_constant_or_unknown_driver']+=1
                    continue
                driver=driver.rsplit('/',1)[0]
                pin_edges+=1
                for d,s in zip(placements,stats):
                    if driver not in d or cell not in d:
                        s['skipped']['missing_placement_edge']+=1;continue
                    a,b=d[driver],d[cell]
                    if a==b:continue
                    for cut in range(min(a,b),max(a,b)):
                        s['edges'][str(cut)+'-'+str(cut+1)]+=1;s['nets'][cut].add(net)
                    if min(a,b)<=1<max(a,b):
                        s['pairs'][members.get(driver,'?')+' -> '+members.get(cell,'?')]+=1
    result={'schema':'amf-static-slr-cut-v1','definition':'Unique net names spanning each SLR seam; pin edges separately counted. Constants/ports/unknown endpoints excluded. Not Vivado routed SLL use; no wire allocation modeled.',
            'netlist_cells':nt,'ordinary_input_pin_edges':pin_edges,'runs':[],
            'slr_transition_cells':dict(collections.Counter(str(placements[0][c])+' -> '+str(placements[1][c]) for c in placements[0]))}
    for run,d,s in zip(runs,placements,stats):
        result['runs'].append({'run':str(run),'assigned_cells':len(d),'cell_slr_counts':dict(collections.Counter(d.values())),'cell_types_by_slr':dict(s['types']),
                              'crossing_pin_edges':dict(s['edges']),'unique_crossing_nets':{str(i)+'-'+str(i+1):len(x) for i,x in enumerate(s['nets'])},
                              'top_1_2_module_pair_pin_edges':s['pairs'].most_common(15),'skipped':dict(s['skipped']),
                              'requested_sha256':digest(run/'placement/requested.tsv')})
    out=ev/'slrc-failure-01/static-connectivity.json'
    assert not out.exists()
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='runs'}))
    for r in result['runs']:print(json.dumps({k:v for k,v in r.items() if k!='cell_types_by_slr'}))
if __name__=='__main__':
    import sys
    main(sys.argv[1])
