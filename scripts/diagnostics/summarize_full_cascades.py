"""Inventory actual URAM and SRL cascade components without modifying the design."""
import argparse,csv,json,hashlib,collections
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--inventory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();csv.field_size_limit(64*1024*1024)
c={x['cell_id']:x for x in csv.DictReader((a.inventory/'cells.tsv').open(),delimiter='\t')}
edges={'URAM':set(),'SRL':set()}
for row in csv.DictReader((a.inventory/'nets.tsv').open(),delimiter='\t'):
    src,_,out=row['drivers'].partition(':')
    if src not in c:continue
    kind=c[src]['primitive'];family='URAM' if kind.startswith('URAM') and out.startswith('CAS_OUT') else 'SRL' if kind.startswith('SRL') and out=='Q31' else None
    if not family:continue
    for sink in row['sinks'].split(','):
        dst,_,pin=sink.partition(':')
        if dst not in c:continue
        if (family=='URAM' and c[dst]['primitive'].startswith('URAM') and pin.startswith('CAS_IN')) or (family=='SRL' and c[dst]['primitive'].startswith('SRL') and pin=='D'):
            edges[family].add((c[src]['cell_name'],c[dst]['cell_name']))
result={}
for family,pairs in edges.items():
    adjacency=collections.defaultdict(set)
    for x,y in pairs:adjacency[x].add(y);adjacency[y].add(x)
    remaining=set(adjacency);components=[]
    while remaining:
        todo=[min(remaining)];group=[]
        while todo:
            x=todo.pop()
            if x not in remaining:continue
            remaining.remove(x);group.append(x);todo.extend(adjacency[x]&remaining)
        components.append(sorted(group))
    result[family]=dict(directed_edges=len(pairs),components=components,size_histogram=dict(collections.Counter(map(len,components))))
result['URAM']['implementation_status']='Linear cascades use movable macros with consecutive same-column, same-SLR sites. Inventory alone is not a placement acceptance result.'
result['SRL']['implementation_status']='Initial packing splits chains at eight cells, with A6LUT external endpoints.'
result['inventory_hashes']={n:hashlib.sha256((a.inventory/n).read_bytes()).hexdigest() for n in ['cells.tsv','nets.tsv']}
a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:{kk:vv for kk,vv in v.items() if kk!='components'} for k,v in result.items() if k!='inventory_hashes'}))
