#!/usr/bin/env python3
"""Summarize immutable route outputs. DCPs are hashed but never copied."""
import collections,csv,hashlib,json,pathlib,re,sys
P=pathlib.Path
repo=P(sys.argv[1]);ident='getrf-e2-sll-route-pair-20261028-130616-093346'
comparison=repo/'experiments/comparisons'/ident
audit=repo/'experiments/runs/getrf-e2-route-bidirectional-audit-20261028-134926'
inspect=repo/'experiments/runs/getrf-e2-route-audit-inspect-20261028-134314'
def readj(p): return json.loads(p.read_text())
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(8*1024**2),b''):h.update(chunk)
 return h.hexdigest()
def tsv(p):
 with p.open() as f:return list(csv.DictReader(f,delimiter='\t'))
state=readj(comparison/'status.json')
assert state['state']=='attempts_finished',state['state']
result={'comparison':str(comparison),'manifest':readj(comparison/'manifest.json'),'cases':{}}
by_net={}
for mode in ['sll-fixed','sll-free']:
 run=repo/'experiments/runs'/(ident+'-'+mode);r=run/'reports';st=readj(run/'status.json')
 timing=(r/'timing-summary.rpt').read_text();section=timing[timing.index('Design Timing Summary'):]
 row=next(l for l in section.splitlines() if re.match(r'^\s*[-\d]+\.\d+\s+[-\d]+\.\d+\s+\d+\s+\d+',l))
 v=row.split();tim={'wns_ns':float(v[0]),'tns_ns':float(v[1]),'setup_failing_endpoints':int(v[2]),'setup_total_endpoints':int(v[3]),'whs_ns':float(v[4]),'ths_ns':float(v[5]),'hold_failing_endpoints':int(v[6]),'hold_total_endpoints':int(v[7]),'wpws_ns':float(v[8])}
 route={}
 for line in (r/'route-status.rpt').read_text().splitlines():
  m=re.match(r'\s*# of (.*?)\.+\s*:\s*(\d+)\s*:',line)
  if m:route[m[1]]=int(m[2])
 drc=(r/'drc.rpt').read_text();header=drc[:drc.index('2. REPORT DETAILS',drc.index('1. REPORT SUMMARY')+30)]
 drc_rows=[]
 for line in header.splitlines():
  cells=[x.strip() for x in line.strip().strip('|').split('|')]
  if len(cells)==4 and cells[3].isdigit():drc_rows.append(dict(rule=cells[0],severity=cells[1],description=cells[2],count=int(cells[3])))
 used=tsv(r/'sll-used.tsv');net_nodes=collections.defaultdict(set);net_columns=collections.defaultdict(set)
 for x in used:net_nodes[x['net']].add(x['node']);net_columns[x['net']].add((x['crossing_slrs'],int(x['physical_column'])))
 by_net[mode]={'nodes':net_nodes,'columns':net_columns}
 cols=[dict(crossing_slrs=x['crossing_slrs'],physical_column=int(x['physical_column']),used_sll_nodes=int(x['used_sll_nodes'])) for x in tsv(r/'sll-columns.tsv')]
 stages={x['stage']:{'seconds':float(x['seconds']),'return_code':int(x['return_code'])} for x in tsv(r/'stages.tsv')}
 result['cases'][mode]={'status':st,'timing':tim,'route_status':route,'drc':drc_rows,'stages':stages,'columns':cols,'sll_rows_by_net_type':dict(collections.Counter(x['net_type'] for x in used)),'sll_rows_by_route_status':dict(collections.Counter(x['route_status'] for x in used)),'actual_sll_net_count':len(net_nodes),'sll_per_net_histogram':dict(collections.Counter(len(x) for x in net_nodes.values()))}
 fixed=st.get('checkpoint',{})
 if fixed:result['cases'][mode]['output_hash_rechecked']=sha(P(fixed['path']))==fixed['sha256']
f=by_net['sll-fixed'];u=by_net['sll-free'];common=set(f['nodes'])&set(u['nodes'])
result['net_comparison']={'common':len(common),'fixed_only':sorted(set(f['nodes'])-set(u['nodes'])),'free_only':sorted(set(u['nodes'])-set(f['nodes'])),'same_exact_sll_set':sum(f['nodes'][n]==u['nodes'][n] for n in common),'same_seam_column_set':sum(f['columns'][n]==u['columns'][n] for n in common),'free_uses_more_sll':sum(len(u['nodes'][n])>len(f['nodes'][n]) for n in common),'free_uses_fewer_sll':sum(len(u['nodes'][n])<len(f['nodes'][n]) for n in common)}
ast=readj(audit/'status.json');assert ast['exit_code']==0,ast
ar=audit/'reports';counts=collections.Counter();nets=set();bad=[]
with (ar/'sll-path-audit.tsv').open() as stream:
 for x in csv.DictReader(stream,delimiter='\t'):
  counts['rows']+=1;counts['remote' if x['expected'] else 'local']+=1;nets.add(x['net'])
  if x['match']!='1' or x['expected']!=x['actual']:bad.append(x)
assert counts['rows']==280438 and len(nets)==8442 and not bad,(counts,len(nets),bad[:1])
result['supplemental_audit']={'run':str(audit),'status':ast,'counts':dict(counts),'nets':len(nets),'mismatches':len(bad),'audit_status':dict(line.split('\t',1) for line in (ar/'audit-status.tsv').read_text().splitlines()),'manifest':readj(audit/'manifest.json'),'graph_diagnosis':readj(inspect/'reports/graph-diagnosis.json')}
result['helper_sha256_after']=sha(repo/'scripts/sll_routes.tcl')
assert result['helper_sha256_after']==sha(comparison/'inputs/sll_routes.tcl')
assert all(v['output_hash_rechecked'] and v['status']['input_hash_unchanged'] for v in result['cases'].values())
result['supplemental_audit']['report_files']={str(p):{'sha256':sha(p),'bytes':p.stat().st_size} for p in [ar/'sll-path-audit.tsv',ar/'sll-source-access-audit.tsv',ar/'audit-status.tsv']}
for path,digest in result['supplemental_audit']['manifest']['inputs'].items():
 assert sha(P(path))==digest,path
result['limitations']=['Input checkpoints are OOC; external ports/clock source are not fully constrained.','Input endpoint locking attributes differ; this is an as-is DCP comparison.','Read-only diagnostics/audit overlapped the free route; elapsed time is not an isolated performance benchmark.','Each DCP ran route_design once; no incremental routing or retry.','Supplemental helper changes only read-only graph traversal, not production exporter, constraints or DCP.']
out=comparison/'derived';out.mkdir(exist_ok=False)
(out/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'path':str(out/'results.json'),'cases':{k:{'timing':v['timing'],'route':v['route_status'],'route_seconds':v['stages']['route']['seconds'],'sll_nodes':sum(c['used_sll_nodes'] for c in v['columns'])} for k,v in result['cases'].items()},'audit':dict(counts),'net_comparison':result['net_comparison']},ensure_ascii=False,indent=2))
