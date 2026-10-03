#!/usr/bin/env python3
"""Collect case-specific QoR and separate functional placement from adaptation."""
import argparse
from datetime import datetime
import json
from pathlib import Path

from finish_r10_full_flow import read_qor
from summarize_runtime_profile import summarize


def collect(run):
    run = Path(run).resolve()
    manifest = json.loads((run / 'manifest.json').read_text())
    status = json.loads((run / 'status.json').read_text())
    amf_source = Path(manifest['placement_source']['run']) if manifest.get('placement_run') else run
    profile = summarize(amf_source)
    result = dict(case=manifest['input_provenance']['case'], run_id=run.name,
                  binary_sha256=manifest['binary_sha256'], status=status,
                  clock_period_ns=manifest['amf_clock_period_ns'],
                  vivado_clock_source=manifest['vivado_clock_source'],
                  configuration='R10 algorithm parameters with documented input-compatibility fixes',
                  input_provenance=manifest['input_provenance'],
                  amf_process_wall_s=profile['amf_process_wall_s'],
                  amf_placement_functional_wall_s=profile['placement_functional_wall_s'],
                  amf_non_placement_categories_s=profile['non_placement_categories_s'],
                  full_flow_stages=manifest['stages'], amf_source_run=str(amf_source),
                  amf_reused=bool(manifest.get('placement_run')))
    if status['state'] == 'completed' and status.get('routing_executed'):
        result['qor'] = read_qor(run, amf_source)
        summary = json.loads((run / 'reports/summary.json').read_text())
        result['vivado_stages_seconds'] = summary['vivado_stages_seconds']
        result['full_flow_wall_s'] = (datetime.fromisoformat(status['finished']) -
                                      datetime.fromisoformat(manifest['started'])).total_seconds()
        result['full_flow_wall_scope'] = 'Backend retry only; reused AMF time reported separately' if manifest.get('placement_run') else 'AMF and backend'
        result['final_dcp'] = str(run / 'reports/getrf_routed.dcp')
        result['final_dcp_sha256'] = status['output_dcp_sha256']
    else:
        result['qor'] = None
        result['note'] = 'AMF profile retained; complete backend acceptance is unavailable.'
    path = run / 'reports/ispd_r10_metrics.json'
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'metrics': str(path), 'status': status}, ensure_ascii=False, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    collect(parser.parse_args().run)
