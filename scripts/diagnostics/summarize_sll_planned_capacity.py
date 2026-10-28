#!/usr/bin/env python3
"""Read-only SLL capacity and planned/actual resource accounting."""
import collections,csv,json,pathlib,subprocess,sys
run=pathlib.Path(sys.argv[1]);out=run/'reports'
cfg=json.loads((run/'config.json').read_text())
table=pathlib.Path(cfg['SllChannelFile'])
with table.open() as f:
    rows=list(csv.DictReader((line for line in f if not line.startswith('#')),delimiter='\t'))
inventory={};supply=collections.defaultdict(set)
for r in rows:
    key=(min(int(r['from']),int(r['to'])),int(r['column']),min(float(r['in_y']),float(r['out_y'])))
    if r['resource'] in inventory and inventory[r['resource']]!=key:raise ValueError('Direction identity mismatch')
    inventory[r['resource']]=key;supply[key].add(r['resource'])
columns=collections.Counter()
for (boundary,column,y),nodes in supply.items():columns[boundary,column]+=len(nodes)
script=r'''
source [lindex $argv 0]
set f [open [lindex $argv 1] w];puts $f "driver\tnode\tfrom\tto"
proc walk {driver items f} {
 foreach c $items {
  if {![dict exists $c pin]} {
   puts $f [join [list $driver [dict get $c resource] [dict get $c from] [dict get $c to]] "\t"]
   walk $driver [dict get $c children] $f
  }
 }
}
foreach record $e2_sll_plan {walk [dict get $record driver] [dict get $record tree] $f}
close $f
'''
tmp=out/'flatten_sll_plan.tcl';tmp.write_text(script)
flat=out/'sll-plan-resources.tsv'
subprocess.run(['tclsh',str(tmp),str(run/'placement/sll-plan.tcl'),str(flat)],check=True)
with flat.open() as f:planned=list(csv.DictReader(f,delimiter='\t'))
def summarize(records):
    counts=collections.Counter();col=collections.Counter();boundary=collections.Counter();resources=set()
    for r in records:
        node=r['node']
        if node in resources:raise ValueError('Duplicate SLL resource '+node)
        resources.add(node);key=inventory[node];counts[key]+=1;col[key[:2]]+=1;boundary[key[0]]+=1
    tile_rows=[dict(boundary=k[0],column=k[1],lower_y=k[2],used=counts[k],capacity=len(v),utilization=counts[k]/len(v)) for k,v in sorted(supply.items())]
    col_rows=[dict(boundary=k[0],column=k[1],used=col[k],capacity=c,utilization=col[k]/c) for k,c in sorted(columns.items())]
    return dict(sll_count=len(resources),boundaries=dict(boundary),
        peak_column_utilization=max(r['utilization'] for r in col_rows),
        columns_over_75=sum(r['utilization']>.75+1e-9 for r in col_rows),
        columns_at_or_over_85=sum(r['utilization']>=.85-1e-9 for r in col_rows),
        tiles_over_80=sum(r['utilization']>.8+1e-9 for r in tile_rows),
        tiles_full=sum(r['used']==r['capacity'] for r in tile_rows),
        tiles_overfull=sum(r['used']>r['capacity'] for r in tile_rows),
        peak_tile_utilization=max(r['utilization'] for r in tile_rows),columns=col_rows,tiles=tile_rows)
result={'scope':'Canonical physical SLL nodes; up/down views share tile and column capacity','planned':summarize(planned)}
netbound=collections.Counter((r['driver'],min(int(r['from']),int(r['to']))) for r in planned)
result['planned']['nets']=len(set(r['driver'] for r in planned))
result['planned']['net_boundary_pairs_with_multiple_sll']=sum(v>1 for v in netbound.values())
path=out/'sll-used.tsv'
if path.exists():
    with path.open() as f:actual=list(csv.DictReader(f,delimiter='\t'))
    result['actual']=summarize(actual);result['actual']['nets']=len(set(r['net'] for r in actual))
    p={r['node'] for r in planned};a={r['node'] for r in actual}
    result['planned_vs_actual']={'missing_nodes':sorted(p-a),'extra_nodes':sorted(a-p),'node_set_equal':p==a}
for label in ['planned','actual']:
    if label not in result:continue
    for group in ['columns','tiles']:
        records=result[label][group]
        with (out/('sll-'+label+'-'+group+'.tsv')).open('w') as f:
            w=csv.DictWriter(f,list(records[0]),delimiter='\t');w.writeheader();w.writerows(records)
        del result[label][group]
projection=out/'sll-planning/sll-qp-projection.tsv'
if projection.exists():
    qp_rows=[];counts=collections.Counter();current=None
    def finish(qp,counts):
        if qp is None:return
        peak=max((n/columns[k] for k,n in counts.items()),default=0)
        qp_rows.append(dict(qp=qp,crossings=sum(counts.values()),peak_column_utilization=peak,
            columns_over_75=sum(n/columns[k]>.75+1e-9 for k,n in counts.items()),
            columns_at_or_over_85=sum(n/columns[k]>=.85-1e-9 for k,n in counts.items())))
    with projection.open() as f:
        for r in csv.DictReader(f,delimiter='\t'):
            qp=int(r['qp'])
            if current!=qp:finish(current,counts);counts=collections.Counter();current=qp
            counts[min(int(r['from']),int(r['to'])),int(r['column'])]+=1
    finish(current,counts)
    if qp_rows:
        with (out/'sll-qp-column-checks.tsv').open('w') as f:
            w=csv.DictWriter(f,list(qp_rows[0]),delimiter='\t');w.writeheader();w.writerows(qp_rows)
        result['all_qp_projections']={'rounds':len(qp_rows),'peak_column_utilization':max(x['peak_column_utilization'] for x in qp_rows),
            'rounds_over_75':sum(x['columns_over_75']>0 for x in qp_rows),
            'rounds_at_or_over_85':sum(x['columns_at_or_over_85']>0 for x in qp_rows)}
(out/'sll-capacity-summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
