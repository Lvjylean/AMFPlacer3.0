#!/usr/bin/env python3
"""Audit DFX implementation separately from timing and process completion."""
import argparse
import csv
import json
from pathlib import Path
import re
from summarize_face_detect_flow import timing, drc
from summarize_full_flow import routing_complete


def summarize(root):
    root = Path(root)
    counts = {}
    for line in (root/'route_status.rpt').read_text().splitlines():
        m = re.search(r'# of\s+(.+?)\s*:\s*(\d+)', line)
        if m: counts[m[1].rstrip('. ')] = int(m[2])
    d = drc(root/'routed_drc.rpt')
    explicit = root/'routed_drc_counts.tsv'
    if explicit.exists():
        observed = {k:int(v) for k,v in (x.split('\t') for x in explicit.read_text().splitlines())}
        if observed['errors'] != d['totals'].get('Error',0) or observed['critical_warnings'] != d['totals'].get('Critical Warning',0):
            raise ValueError('DRC report/count mismatch')
    rows = list(csv.DictReader((root/'placement_retention.tsv').open(), delimiter='\t'))
    matches = sum(r['requested'] == r['loc']+'/'+r['bel'].split('.')[-1] for r in rows)
    result = dict(routing_counts=counts, routing_complete=routing_complete(counts), drc=d,
                  timing={stage:timing(root/(stage+'_timing.rpt')) for stage in ('imported','placed','routed')},
                  final_rm_placement=dict(requested=len(rows), exact_matches=matches, moved=len(rows)-matches),
                  pr_verify_full_check=(root/'pr_verify_passed.txt').read_text().strip()=='full_check_passed')
    result['implementation_verified'] = (result['routing_complete'] and result['pr_verify_full_check'] and
        d['totals'].get('Error',0)==0 and d['totals'].get('Critical Warning',0)==0 and len(rows)>0)
    result['timing_met'] = all(result['timing']['routed'][k]>=0 for k in ('wns_ns','whs_ns','wpws_ns'))
    result['vivado_stage_seconds'] = {}
    for line in (root/'stage_times.tsv').read_text().splitlines():
        name,seconds,code=line.split('\t')
        result['vivado_stage_seconds'][name] = dict(seconds=float(seconds), exit_code=int(code))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run');a=p.parse_args()
    print(json.dumps(summarize(a.run),indent=2))
