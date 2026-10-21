"""Compile cached topology plus current-shell ownership into a sparse policy.

No Vivado calls or coordinate repairs. All DSP sites are retained for joint
entrance allocation; SLICE sites are retained when static routing affects an
external node or its first hop. Complete evidence remains in the parent cache.
"""
import collections
import csv
import gzip
import json
from pathlib import Path
import re
import time

VERSION = 'fixed-static-local-entrance-v2'

def rows(path):
    with (gzip.open(path, 'rt') if str(path).endswith('.gz') else open(path)) as f:
        yield from csv.DictReader(f, delimiter='\t')

def compile_policy(topology, occupied, owners_file, output, budget_seconds, owner_marker=None, session_token=None):
    begin=time.monotonic(); topology=Path(topology); output=Path(output)
    def deadline():
        if time.monotonic()-begin>=budget_seconds:
            raise TimeoutError('Local pin-policy generation deadline exceeded')
    occupied_nodes={r['node'] for r in rows(occupied)}
    # Keep actual routed-net identity in evidence. Port identities are stable
    # across module replacement, whereas old primitive or numeric IDs are not.
    graph={};affected=set()
    for i,r in enumerate(rows(topology/'external_graph.tsv.gz')):
        k=(r['node'],r['direction']);adj=r['adjacent_nodes'].split('|') if r['adjacent_nodes'] else []
        if k in graph:raise ValueError('Duplicate external graph key')
        graph[k]=adj
        if r['node'] in occupied_nodes or any(n in occupied_nodes for n in adj):affected.add(k)
        if i%10000==0:deadline()
    sites=set();sitepins=[]
    for i,r in enumerate(rows(topology/'site_pins.tsv.gz')):
        nodes=r['nodes'].split('|') if r['nodes'] else []
        sitepins.append((r,nodes))
        if r['site'].startswith('DSP') or any((n,r['direction']) in affected for n in nodes):
            sites.add((r['module'],r['site']))
        if i%10000==0:deadline()
    selected={};wanted=set()
    for r,nodes in sitepins:
        if (r['module'],r['site']) not in sites:continue
        selected[r['site_pin']]=(r,nodes)
        for n in nodes:
            wanted.add(n);wanted.update(graph[(n,r['direction'])])
    del sitepins
    attrs={}
    for i,r in enumerate(rows(topology/'node_attributes.tsv.gz')):
        if r['node'] in wanted:
            attrs[r['node']]=(int(r['is_gnd']),int(r['is_vcc']),int(r['is_bad']))
        if i%10000==0:deadline()
    if set(attrs)!=wanted:raise ValueError('Incomplete resource attributes')
    ids={n:i for i,n in enumerate(sorted(wanted))}
    bels=[]
    for i,r in enumerate(rows(topology/'bel_pins.tsv.gz')):
        if (r['module'],r['site']) in sites and r['site'].startswith('SLICE_'):
            full,pin=r['bel_pin'].rsplit('/',1);bel=full.rsplit('/',1)[1]
            if re.fullmatch(r'[A-H](?:[56]LUT|FF2?)',bel):
                sps=r['site_pins'].split('|') if r['site_pins'] else []
                if any(p not in selected for p in sps):raise ValueError('Incomplete selected site pin')
                bels.append((r['module'],r['site'],bel,pin,r['direction'],sps))
        if i%20000==0:deadline()
    prepared_seconds=time.monotonic()-begin
    wait_started=time.monotonic()
    if owner_marker:
        marker=Path(owner_marker)
        while not marker.is_file():
            deadline();time.sleep(.05)
        fields=marker.read_text().split()
        if len(fields)%2 or dict(zip(fields[::2],fields[1::2])).get('token')!=session_token:
            raise ValueError('Owner marker identity mismatch')
    owner_wait_seconds=time.monotonic()-wait_started
    owners={}
    for r in rows(owners_file):
        if r['node'] in owners or r['fixed'] not in ('1','TRUE'):
            raise ValueError('Duplicate or nonfixed owner')
        owners[r['node']]=r
    if set(owners)!=occupied_nodes:raise ValueError('Ownership inventory differs from certified occupancy')
    def paths(pins):
        paths=set()
        for p in pins:
            r,nodes=selected[p]
            for n in nodes:
                adjacent=graph[(n,r['direction'])]
                for hop in adjacent:paths.add((ids[n],ids[hop]))
        return ';'.join(str(a)+','+str(b) for a,b in sorted(paths))
    output.mkdir(parents=True,exist_ok=False)
    count=collections.Counter()
    with (output/'local_access.tsv').open('w') as f:
        f.write('AMF_LOCAL_ACCESS\t2\n')
        for n,i in ids.items():
            g,v,b=attrs[n];r=owners.get(n,{})
            if g and v:raise ValueError('Resource is both GND and VCC')
            tie=0 if g else (1 if v else -1)
            constant={'GROUND':0,'POWER':1}.get(r.get('type'),-1)
            f.write('\t'.join(map(str,['N',i,n,tie,b,int(bool(r)),constant,r.get('ports','')]))+'\n')
        for module,site in sorted(sites):f.write('S\t'+module+'\t'+site+'\n')
        for module,site,bel,pin,direction,pins in bels:
            f.write('\t'.join(['P',module,site,bel,pin,direction,paths(pins)])+'\n');count['slice_bel_pins']+=1
        for sp,(r,nodes) in sorted(selected.items()):
            if not r['site'].startswith('DSP'):continue
            pin=sp.rsplit('/',1)[1]
            f.write('\t'.join(['P',r['module'],r['site'],'DSP48E2',pin,r['direction'],paths([sp])])+'\n')
            count['dsp_site_pins']+=1
    deadline()
    result=dict(policy=VERSION,seconds=time.monotonic()-begin,resources=len(ids),occupied_nodes=len(owners),
                sites=len(sites),counts=count,preparation_seconds=prepared_seconds,owner_wait_seconds=owner_wait_seconds,first_hop_only=True,complete_parent_evidence_reused=True,
                unknown_internal_access_is_not_certified=True)
    (output/'compile.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
