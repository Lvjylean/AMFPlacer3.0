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


def summarize(root):
    reports=root/'reports'
    result={}
    manifest=json.loads((root/'manifest.json').read_text())
    result['stages']=manifest['stages']
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
