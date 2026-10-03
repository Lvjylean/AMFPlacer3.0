#!/usr/bin/env python3
"""Collect completed case outcomes without treating routing as timing closure."""
import json
from pathlib import Path
import re
import sys

batch=Path(sys.argv[1]).resolve()
state=json.loads((batch/'status.json').read_text())
catalog={x['case']:x for x in json.loads((batch/'reports/preparation/catalog.json').read_text())}
results=[]
recoveries=json.loads((batch/'recovery_runs.json').read_text()) if (batch/'recovery_runs.json').exists() else {}
for case in state['cases']:
    source=catalog[case['case']]
    record=dict(case=case['case'],state=case['state'],run_directory=case.get('run_directory'),
                amf_period_ns=source['amf_period_ns'],vivado_period_ns=source['vivado_period_ns'],
                total_elapsed_seconds=case.get('elapsed_seconds'),exit_code=case.get('exit_code'))
    if case['case'] in recoveries:
        recovery=recoveries[case['case']]
        record['initial_attempt']=dict(run_directory=record['run_directory'],state=record['state'],exit_code=record['exit_code'])
        record.update(run_directory=recovery['run_directory'],recovery=recovery,total_elapsed_seconds=None,exit_code=None)
    if record['run_directory']:
        run=Path(record['run_directory'])
        record['flow_status']=json.loads((run/'status.json').read_text())
        if 'recovery' in record:
            record['state']=record['flow_status']['state']
            if record['state']=='completed':record['exit_code']=0
        path=run/'reports/summary.json'
        if path.exists():
            summary=json.loads(path.read_text())
            for key in ('strict_import_verified','implementation_verified','routing_complete','timing_met',
                        'drc_errors','drc_counts','timing','missing_ooc_clock_source_warning','vivado_stages_seconds'):
                record[key]=summary.get(key)
            record['amf_cells']=summary['amf_coverage']['input_cells']
            record['amf_elapsed_seconds']=next((s['elapsed_seconds'] for s in summary['stages'] if s['name']=='amf'),None)
            if record['amf_elapsed_seconds'] is None and 'initial_attempt' in record:
                initial=json.loads((Path(record['initial_attempt']['run_directory'])/'manifest.json').read_text())
                record['amf_elapsed_seconds']=next(s['elapsed_seconds'] for s in initial['stages'] if s['name']=='amf')
            report=(run/'reports/utilization.rpt').read_text()
            match=re.search(r'\| Total SLLs Used\s*\|\s*(\d+)',report)
            record['actual_slls_used']=int(match.group(1)) if match else None
            record['routed_dcp']=str(run/'reports/getrf_routed.dcp')
            record['routed_dcp_sha256']=record['flow_status'].get('output_dcp_sha256')
    results.append(record)
resolved='completed' if len(results)==3 and all(r['state']=='completed' for r in results) else ('running' if any(r['state']=='running' for r in results) else 'failed')
report=dict(state=resolved,original_batch_state=state['state'],scope='U250 compute-core OOC; MemN2N uses documented ROM initialization repairs',
            clock_scope='Original per-case periods. Physical parent clock sources and board I/O budgets unspecified.',
            cases=results,dcp_downloaded=False)
output=batch/'reports/results.json'
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
