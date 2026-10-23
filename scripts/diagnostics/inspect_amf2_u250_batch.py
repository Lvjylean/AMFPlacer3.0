#!/usr/bin/env python3
"""Read-only compact status for the recorded AMF2 U250 core batch."""
import json
from pathlib import Path
import re

root=Path(__file__).resolve().parents[2]/'experiments/runs'
batch=root/'amf2-large-u250-core-batch-20260930-01'
state=json.loads((batch/'status.json').read_text())
print('BATCH',state['state'],[(x['case'],x['state']) for x in state['cases']])
for run in sorted(root.glob('amf2-*-u250-core-r10*-full-20260930-*')):
    state=json.loads((run/'status.json').read_text())
    print(run.name,json.dumps({k:state[k] for k in ('state','stage','error') if k in state}))
    summary=run/'reports/summary.json'
    if summary.exists():
        data=json.loads(summary.read_text())
        print('SUMMARY',json.dumps(dict(routed=data['routing_complete'],drc_errors=data['drc_errors'],wns=data['timing']['wns_ns'],whs=data['timing']['whs_ns'])))
    if state['state']=='running':
        log=run/'logs/vivado.log'
        if not log.exists():log=run/'logs/amf.log'
        if log.exists():
            with log.open('rb') as f:
                f.seek(max(0,log.stat().st_size-6000)); tail=f.read().decode(errors='replace')
            tail=re.sub(r'\x1b\[[0-9;]*m','',tail)
            print(log.name,tail[-1400:])
