#!/usr/bin/env python3
"""Aggregate evidence without turning partial observability into equivalence proof."""
import csv
import datetime
import hashlib
import json
import re
from pathlib import Path
import sys

root=Path(sys.argv[1])
def read(name):return json.loads((root/name).read_text())
native=read('native-comparison.json')
inventory=read('inventory-comparison.json')
constraints=read('constraint-comparison.json')
gap=read('protected-init-coverage-gap.json')
checks=[]
for stage in ['input_import','placed_routed']:
    with (root/stage/'audit-events.tsv').open() as f:
        checks.extend(list(csv.DictReader(f,delimiter='\t')))
failed=[x for x in checks if x['status']!='0']
edif={}
for pair in ['input-vs-imported','imported-vs-placed','placed-vs-routed']:
    edif[pair]=read('ledgers/'+pair+'.classification.json')
visible=native['all_tables_identical'] and all(x['structural_equivalence_pass'] for x in edif.values())
source=read('source-integrity-recheck.json')
reports=root/'placed_routed/routed'
def number(report,label):
    match=re.search(re.escape(label)+r'\.+\s*:\s*(\d+)',(reports/report).read_text())
    if not match:raise ValueError('Missing report metric: '+label)
    return int(match[1])
timing_text=(reports/'timing-summary.rpt').read_text().split('Design Timing Summary',1)[1]
timing=None
for line in timing_text.splitlines():
    fields=line.split()
    if len(fields)!=12:continue
    try:values=list(map(float,fields))
    except ValueError:continue
    timing=dict(wns_ns=values[0],tns_ns=values[1],setup_violations=int(values[2]),setup_endpoints=int(values[3]),whs_ns=values[4],ths_ns=values[5],hold_violations=int(values[6]),hold_endpoints=int(values[7]),wpws_ns=values[8])
    break
assert timing is not None,'Timing summary numeric row missing'
drc_rows=re.findall(r'^\|\s*(\S+)\s*\|\s*(Error|Critical Warning|Warning|Advisory)\s*\|.*?\|\s*(\d+)\s*\|',(reports/'drc-no-waivers.rpt').read_text(),re.M)
assert drc_rows,'No parsed DRC summary rows; do not infer zero violations'
physical=dict(placed_logical_cells=number('place-status.rpt','# of placed cells'),unplaced_cells=number('place-status.rpt','# of unplaced cells'),site_errors=number('place-status.rpt','# of sites with errors'),routable_nets=number('route-status.rpt','# of routable nets'),fully_routed_nets=number('route-status.rpt','# of fully routed nets'),routing_errors=number('route-status.rpt','# of nets with routing errors'),drc_errors=sum(int(n) for rule,severity,n in drc_rows if severity=='Error'),drc_critical_warnings=sum(int(n) for rule,severity,n in drc_rows if severity=='Critical Warning'),drc_rules=[dict(rule=rule,severity=severity,violations=int(n)) for rule,severity,n in drc_rows],scope='default OOC ruledeck, no waivers; warnings remain and some connectivity DRCs are unavailable in OOC')
record={
    'completed_at':datetime.datetime.now().astimezone().isoformat(),
    'run_id':read('manifest.json')['run_id'],
    'audit_evidence_directory':str(root),
    'overall_unqualified_signoff':'NOT_PASSED',
    'visible_logic_invariants':'PASS' if visible else 'FAIL',
    'complete_functional_equivalence':'NOT_PROVEN',
    'functional_equivalence_blocker':gap,
    'strict_timing_constraint_coverage':'FAIL',
    'constraint_coverage_evidence':constraints,
    'native_object_comparison':native,
    'edif_stage_comparisons':edif,
    'primitive_and_placement_inventory':inventory,
    'vivado_report_commands':{'executed':len(checks),'failed':failed},
    'existing_ooc_timing':timing,
    'physical_ooc_checks':physical,
    'source_dcp_integrity':source,
    'formal_sat_or_lec_run':False,
    'limits':['326094 protected INIT values are unreadable; equality of blanks is not functional equivalence.','Missing real interface budgets and HD.CLK_SRC prevent system timing sign-off.','Single-design results do not establish placer correctness for all designs.'],
}
assert not failed,'Failed Vivado audit reports must be resolved before final aggregation'
assert all(x['unchanged'] for x in source.values()),'Source checkpoint changed'
(root/'audit-results.json').write_text(json.dumps(record,ensure_ascii=False,indent=2))
print(json.dumps({k:record[k] for k in ['overall_unqualified_signoff','visible_logic_invariants','complete_functional_equivalence','strict_timing_constraint_coverage']},ensure_ascii=False,indent=2))
