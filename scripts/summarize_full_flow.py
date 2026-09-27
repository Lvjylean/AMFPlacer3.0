"""Separate tool completion, actual routing, DRC, timing and AMF retention."""
import json
import re
from summarize_face_detect_flow import timing, drc


def routing_complete(counts):
    failures = {k:v for k,v in counts.items() if any(s in k.lower() for s in ("unrouted", "partially routed", "routing errors", "conflict"))}
    return ("routable nets" in counts and "fully routed nets" in counts
            and counts["routable nets"] == counts["fully routed nets"]
            and bool(failures) and all(v == 0 for v in failures.values()))


def missing_clock_source_warning(log):
    return any(code in log for code in ('[Timing 38-242]', '[Route 35-197]'))


def numerical_guard_audit(log):
    """Counts refer to solver iterations / weight updates, not unique cells or nets."""
    result={'solver_calls':0,'repaired_row_events':0,'rollback_calls':0,
            'unconverged_calls':0,'maximum_diagonal_delta':0.0,
            'weight_updates':0,'saturated_edge_events':0,'invalid_edge_events':0}
    for line in log.splitlines():
        if 'QP_GUARD axis=' in line:
            fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
            result['solver_calls']+=1
            result['repaired_row_events']+=int(fields['repaired'])
            result['rollback_calls']+=int(fields['rollback'])
            result['unconverged_calls']+=int(fields['converged'])==0
            result['maximum_diagonal_delta']=max(result['maximum_diagonal_delta'],float(fields['max_diagonal_delta']))
        if 'TIMING_WEIGHT_GUARD cap=' in line:
            fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
            result['weight_updates']+=1
            result['saturated_edge_events']+=int(fields['saturated_edges'])
            result['invalid_edge_events']+=int(fields['invalid_edges'])
            result['enhancement_cap']=float(fields['cap'])
    return result


def summarize(root):
    reports=root/'reports'
    result={}
    manifest=json.loads((root/'manifest.json').read_text())
    result['stages']=manifest['stages']
    acceptance=reports/'import_acceptance.tsv'
    result['strict_import_verified']=None
    if acceptance.exists():
        values=dict(line.split('\t') for line in acceptance.read_text().splitlines())
        result['import_acceptance']={k:(v if k=='policy' else int(v)) for k,v in values.items()}
        m=result['import_acceptance']
        result['strict_import_verified']=(m['policy']=='strict'
            and all(m[k]==m['requested'] for k in ('present','placed','exact_loc_bel_matches','exact_original_loc_bel_matches'))
            and all(m[k]==0 for k in ('rejection_events','srl_violations','cascade_violations')))
    amf_log=root/'logs/amf.log'
    if amf_log.exists():result['numerical_guard']=numerical_guard_audit(amf_log.read_text(errors='replace'))
    result['vivado_stages_seconds']={}
    for line in (reports/'stages.tsv').read_text().splitlines()[1:]:
        name, seconds=line.split('\t')
        result['vivado_stages_seconds'][name]=float(seconds)
    for name in ('amf_coverage','imported_placement','placed_placement','routed_placement','imported_cascades','placed_cascades','routed_cascades','srl_cascades','imported_srl_cascades','placed_srl_cascades','routed_srl_cascades'):
        p=reports/(name+'.json')
        if p.exists():result[name]=json.loads(p.read_text())
    counts={}
    for line in (reports/'route_status.rpt').read_text().splitlines():
        m=re.search(r'# of\s+(.+?)\s*:\s*(\d+)',line)
        if m:counts[m[1].rstrip('. ')]=int(m[2])
    result['routing_counts']=counts
    result['routing_complete']=routing_complete(counts)
    coverage=result['amf_coverage']
    result['amf_export_complete']=(coverage['input_cells']==coverage['assigned_cells'] and not coverage['missing_by_type'])
    result['drc']=drc(reports/'drc.rpt');result['drc_errors']=result['drc']['totals'].get('Error',0)
    result['drc_counts']=json.loads((reports/'drc_counts.json').read_text())
    if result['drc_errors'] != result['drc_counts']['errors']:
        raise ValueError('DRC report and Vivado violation count disagree')
    result['implementation_verified']=(result['amf_export_complete'] and result['routing_complete'] and result['drc_errors']==0
        and all(result.get(name,{}).get('violations')==0 for name in ('routed_cascades','routed_srl_cascades')))
    result['timing']=timing(reports/'timing_summary.rpt')
    result['timing_met']=all(result['timing'][k]>=0 for k in ('wns_ns','whs_ns','wpws_ns'))
    log=(root/'logs/vivado.log').read_text(errors='replace')
    result['missing_ooc_clock_source_warning']=missing_clock_source_warning(log)
    for name in ('backend_boundaries','timing_sample_analysis','effective_parameters'):
        path=reports/'physical'/(name+'.json')
        if path.exists():result[name]=json.loads(path.read_text())
    for name in ('imported_placement','placed_placement','routed_placement'):
        r=result.get(name)
        if r:r['exact_retention_ratio']=r['exact_loc_bel_matches']/r['requested']
    return result
