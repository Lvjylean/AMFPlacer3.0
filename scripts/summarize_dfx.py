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
    allowed=set((root/'allowed_sites.txt').read_text().splitlines())
    blocked=set((root/'blocked_sites.txt').read_text().splitlines())
    domain_violations=[r['cell'] for r in rows if r['loc'] not in allowed or r['loc'] in blocked]
    matches = sum(r['requested'] == r['loc']+'/'+r['bel'].split('.')[-1] for r in rows)
    result = dict(routing_counts=counts, routing_complete=routing_complete(counts), drc=d,
                  timing={stage:timing(root/(stage+'_timing.rpt')) for stage in ('imported','placed','routed')},
                  final_rm_placement=dict(requested=len(rows), exact_matches=matches, moved=len(rows)-matches,
                    site_matches=sum(r['requested'].split('/')[0]==r['loc'] for r in rows),
                    domain_violations=domain_violations),
                  pr_verify_full_check=(root/'pr_verify_passed.txt').read_text().strip()=='full_check_passed')
    imp=root/'import_retention.tsv'
    if imp.exists():
        imported=list(csv.DictReader(imp.open(),delimiter='\t'))
        result['imported_rm_placement']=dict(requested=len(imported),exact_matches=sum(x['requested']==x['loc']+'/'+x['bel'].split('.')[-1] for x in imported))
    result['import_policy']='active-RM native remapping audited; static locked; failed placement command aborts'
    audit_file=root/'static_placement_audit.tsv'
    if audit_file.exists():
        audit_rows=[line.split('\t') for line in audit_file.read_text().splitlines()]
        result['static_placement_audit']=dict(
            checks=[dict(stage=r[0],status=r[1],reason=r[2]) for r in audit_rows],
            all_recorded_checks_passed=bool(audit_rows) and all(r[1]=='passed' for r in audit_rows),
            skipped=any(r[1]=='skipped' for r in audit_rows))
    result['implementation_verified'] = (result['routing_complete'] and result['pr_verify_full_check'] and
        d['totals'].get('Error',0)==0 and d['totals'].get('Critical Warning',0)==0 and len(rows)>0 and not domain_violations)
    result['timing_met'] = all(result['timing']['routed'][k]>=0 for k in ('wns_ns','whs_ns','wpws_ns'))
    result['vivado_stage_seconds'] = {}
    for line in (root/'stage_times.tsv').read_text().splitlines():
        name,seconds,code=line.split('\t')
        result['vivado_stage_seconds'][name] = dict(seconds=float(seconds), exit_code=int(code))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run');a=p.parse_args()
    print(json.dumps(summarize(a.run),indent=2))
