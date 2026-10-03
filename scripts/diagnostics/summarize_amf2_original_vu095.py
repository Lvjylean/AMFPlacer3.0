#!/usr/bin/env python3
"""Read-only live summary: never confuse an AMF exit with completed routing."""
import datetime as dt
import json
from pathlib import Path
import sys


def read(path):
    return json.loads(path.read_text()) if path.exists() else None


def summarize(batch):
    catalog=read(batch/'catalog.json')
    amf=read(batch/'amf_status.json')
    backend=read(batch/'backend_status.json')
    retries=read(batch/'backend_retries.json') or []
    rows=[]
    for item in catalog:
        row=dict(case=item['case'],original_cells=item['cells'],
                 amf_clock_period_ns=float(read(Path(item['config']))['ClockPeriod']),
                 paper_case=item['case']!='optimsoc')
        audit=read(batch/'reports'/(item['case']+'-connectivity')/'comparison-canonical.json')
        row['connectivity_audit']=audit
        for name,status in [('amf',amf),('backend',backend)]:
            entry=next((x for x in (status or {}).get('cases',[]) if x['case']==item['case']),None)
            if name=='backend':
                attempts=([dict(entry)] if entry else [])+[dict(x) for x in retries if x['case']==item['case']]
                row['backend_attempts']=attempts
                if attempts:entry=dict(attempts[-1])
            row[name]=entry or dict(state='queued')
            run=(entry or {}).get('run_directory')
            if not run:
                log=batch/'logs'/(item['case']+'-'+name+'.log')
                if log.exists():
                    run=next((s for s in log.read_text().splitlines() if s.startswith(str(batch.parent))),None)
            if run:
                directory=Path(run)
                row[name]['run_directory']=run
                row[name]['run_status']=read(directory/'status.json')
                row[name]['summary']=read(directory/'reports/summary.json')
                row[name]['manifest']=str(directory/'manifest.json')
                if name=='backend':
                    for report in ['amf_coverage','imported_placement','placed_placement','routed_placement','export_name_aliases']:
                        value=read(directory/'reports'/(report+'.json'))
                        if value is not None:row[name][report]=value
        rows.append(row)
    states=[row['backend']['state'] for row in rows]
    effective='completed' if all(x=='completed' for x in states) else 'running' if any(x in ('running','queued') for x in states) else 'failed'
    return dict(updated=dt.datetime.now().astimezone().isoformat(),batch=str(batch),cases=rows,
                upstream_commit='70d98288153046ea4fd07190e748b6530e3042f5',
                binary_sha256='f92a3065e43e92a3fb557201910d0c6bdc2f76f74a46af4a2655accc81577e21',
                amf_state=(amf or {}).get('state'),backend_state=effective,
                original_backend_batch_state=(backend or {}).get('state'),overall_state=effective,
                limitations=['Vivado 2024.2 differs from paper 2020.2/2021.2',
                             'OptimSoC was excluded by AMF2 paper; supplementary experiment',
                             'Complete original VU095 netlists differ from previous U250 OOC cores',
                             'Original Tcl handoff permits Vivado placement completion/repair; inspect retention and DRC',
                             'Pin audit covers canonical leaf connectivity, not formal equivalence or top-level port wiring'])


if __name__=='__main__':
    batch=Path(sys.argv[1]).resolve()
    result=summarize(batch)
    target=batch/'reports/results.json'
    temporary=target.with_suffix('.tmp')
    temporary.write_text(json.dumps(result,indent=2)+'\n');temporary.replace(target)
    print(json.dumps({k:result[k] for k in ('updated','overall_state','amf_state','backend_state')}))
