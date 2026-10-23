#!/usr/bin/env python3
"""Collect the recorded SA-ratio full-flow comparison without copying DCPs."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re


def read_json(path):
    return json.loads(path.read_text()) if path.exists() else None


def timing_accounting(amf_text, times):
    """Separate measured export intervals without claiming complete core timing."""
    pending, intervals, issues = {}, [], []
    pattern = r'ParallelCLBPacker: (dumping|dumped) placementTcl archieve to: (.*?) \(elapsed time: ([0-9.]+) s\)'
    for kind, path, value in re.findall(pattern, amf_text):
        stamp = float(value)
        if kind == 'dumping':
            if path in pending:
                issues.append('Repeated export start: ' + path)
            pending[path] = stamp
        else:
            start = pending.pop(path, None)
            if start is None or stamp < start:
                issues.append('Unmatched or reversed export end: ' + path)
            else:
                intervals.append(dict(path=path, start_seconds=start,
                                      end_seconds=stamp, seconds=stamp-start))
    issues.extend('Unfinished export: ' + path for path in pending)
    export = sum(v['seconds'] for v in intervals) if intervals and not issues else None
    process = times.get('amf_process')
    net = process - export if process is not None and export is not None and process >= export else None
    return dict(
        schema='placement-timing-accounting-v1',
        amf_process_seconds=process,
        amf_recorded_tcl_export_seconds=export,
        amf_run_excluding_recorded_tcl_export_seconds=net,
        amf_core_placement_seconds=None,
        vivado_import_seconds=times.get('import'),
        vivado_open_seconds=times.get('open'),
        vivado_placement_seconds=times.get('place'),
        vivado_routing_seconds=times.get('route'),
        export_intervals=intervals, parsing_issues=issues,
        coverage='partial: other AMF input/output and diagnostic work is not fully timed',
        note='Process time includes export; do not add it twice. Missing core timing is not zero.')


def collect_run(root, run_id):
    run = root / 'experiments/runs' / run_id
    manifest = read_json(run / 'manifest.json')
    status = read_json(run / 'status.json')
    config = read_json(run / 'config.json')
    summary = read_json(run / 'reports/summary.json')
    result = dict(run_id=run_id, status=status, manifest=manifest, config=config)
    result['effective_sa_ratio'] = float(config['Simulated Annealing y2xRatio']) if 'Simulated Annealing y2xRatio' in config else float(config['y2xRatio']) * 0.8
    text = ''
    log = run / 'logs/amf.log'
    if log.exists():
        text = re.sub(r'\x1b\[[0-9;]*m', '', log.read_text(errors='replace'))
        result['sa_ratio_log'] = [s for s in text.splitlines() if 'SA effective y2xRatio' in s]
        result['packing_column_fallback'] = [s for s in text.splitlines() if 'PACKING_COLUMN_FALLBACK' in s]
        hpwl = re.findall(r'Current Total HPWL = ([0-9.]+)', text)
        if hpwl:
            result['final_amf_hpwl'] = float(hpwl[-1])
    if summary:
        result['verification'] = {k: summary.get(k) for k in (
            'strict_import_verified', 'import_acceptance', 'routing_complete',
            'routing_counts', 'drc_counts', 'implementation_verified', 'timing_met',
            'missing_ooc_clock_source_warning', 'routed_placement',
            'routed_cascades', 'routed_srl_cascades', 'numerical_guard')}
        result['timing'] = summary['timing']
        result['backend_boundaries'] = summary.get('backend_boundaries', {}).get('stages', {}).get('routed')
        times = dict(summary['vivado_stages_seconds'])
        times.update({s['name'] + '_process': s['elapsed_seconds'] for s in summary['stages']})
        if status.get('finished'):
            times['total_wall'] = (datetime.fromisoformat(status['finished']) -
                                   datetime.fromisoformat(manifest['started'])).total_seconds()
        result['seconds'] = times
        result['timing_accounting'] = timing_accounting(text, times)
    clocks = run / 'reports/clocks.rpt'
    if clocks.exists():
        result['clock_report'] = clocks.read_text()
    backend_log = run / 'logs/vivado.log'
    if backend_log.exists():
        backend_text = backend_log.read_text(errors='replace')
        result['routing_global_iterations'] = [int(i) for i in re.findall(
            r'^Phase 5\.\d+ Global Iteration (\d+)\s*$', backend_text, re.MULTILINE)]
        result['routing_timing_progress'] = [s for s in backend_text.splitlines()
                                             if 'Timing Summary | WNS=' in s]
        result['estimated_sll_demand'] = {}
        for match in re.finditer(r'SLR \[([0-9-]+)\](.*?)Demand:\s*(\d+) Available:\s*(\d+) Utilization\(%\):\s*([0-9.]+)', backend_text):
            columns = [(int(n), int(percent)) for n, percent in
                       re.findall(r'(\d+)\s*\(\s*(\d+)%\)', match[2])]
            result['estimated_sll_demand'][match[1]] = dict(
                total=int(match[3]), available=int(match[4]), percent=float(match[5]),
                columns=[dict(demand=n, percent=percent) for n, percent in columns])
        congestion_start = backend_text.find('Initial Estimated Congestion')
        if congestion_start >= 0:
            congestion_end = backend_text.find('Congestion Report', congestion_start)
            if congestion_end >= 0:
                result['initial_estimated_congestion_table'] = backend_text[congestion_start:congestion_end]
    timing_report = run / 'reports/timing_summary.rpt'
    if timing_report.exists():
        report_text = timing_report.read_text()
        start = report_text.find('Max Delay Paths')
        end = report_text.find('Min Delay Paths', start)
        if start >= 0 and end >= 0:
            section = report_text[start:end]
            crossings = [list(map(int, pair)) for pair in re.findall(r'SLR Crossing\[(\d+)->(\d+)\]', section)]
            result['worst_setup_path'] = dict(report_section=section, slr_crossings=crossings)
            delays = re.search(r'Data Path Delay:\s*([0-9.]+)ns\s*\(logic ([0-9.]+)ns.*?route ([0-9.]+)ns', section)
            if delays:
                result['worst_setup_path'].update(dict(zip(
                    ('data_delay_ns', 'logic_delay_ns', 'route_delay_ns'), map(float, delays.groups()))))
            crossing_delays = re.findall(r'net \(fo=\d+, routed\)\s+([0-9.]+).*\n\s*SLR Crossing', section)
            result['worst_setup_path']['crossing_net_route_delays_ns'] = list(map(float, crossing_delays))
    if status.get('state') == 'completed' and status.get('routing_executed'):
        result['routed_dcp'] = str(run / 'reports/getrf_routed.dcp')
        result['routed_dcp_sha256'] = status.get('output_dcp_sha256')
    return result


def compare(root, directory):
    meta = read_json(directory / 'comparison.json')
    old = collect_run(root, meta['baseline'])
    new = collect_run(root, meta['candidate'])
    ignore = {'dumpDirectory', 'BoundaryReportDirectory'}
    config_delta = {k: [old['config'].get(k), new['config'].get(k)]
                    for k in sorted(old['config'].keys() | new['config'].keys())
                    if k not in ignore and old['config'].get(k) != new['config'].get(k)}
    a, b = old['manifest'], new['manifest']
    checks = dict(input_dcp_identical=a['input_dcp_sha256'] == b['input_dcp_sha256'],
                  inputs_identical=a['inputs'] == b['inputs'],
                  clock_period_identical=a['amf_clock_period_ns'] == b['amf_clock_period_ns'],
                  strict_import_both=a['import_policy'] == b['import_policy'] == 'strict')
    checks['frozen_runner_files'] = {
        k: (h == b['runner_files_sha256'][k]) if k in b.get('runner_files_sha256', {}) else None
        for k, h in a.get('runner_files_sha256', {}).items()
    }
    for key in ('backend_script_sha256', 'boundary_diagnostic_sha256'):
        checks[key + '_identical'] = (a.get(key) == b.get(key)) if b.get(key) else None
    result = dict(schema='sa-ratio-full-flow-comparison-v1',
                  collected_at=datetime.now().astimezone().isoformat(),
                  baseline=old, candidate=new, comparability=checks,
                  config_delta=config_delta, source_delta=meta.get('source_diff'),
                  caveats=[
                      'Historical baseline; server load was not controlled.',
                      'Candidate also includes existing CLB packing robustness changes; not a pure single-code-change ablation.',
                      'OOC clock-source warnings limit board-level interpretation.',
                      'Geometric boundary crossings are not SLL usage.',
                      'One GETRF run does not establish general or statistically significant improvement.'
                  ])
    if old.get('timing') and new.get('timing'):
        result['timing_delta_candidate_minus_baseline'] = {
            k: new['timing'][k] - old['timing'][k] for k in old['timing']}
        result['seconds_delta_candidate_minus_baseline'] = {
            k: new['seconds'][k] - old['seconds'][k]
            for k in old['seconds'].keys() & new['seconds'].keys()}
        result['runtime_change_percent'] = {
            k: 100 * (new['seconds'][k] / old['seconds'][k] - 1)
            for k in old['seconds'].keys() & new['seconds'].keys() if old['seconds'][k]}
    (directory / 'results.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({k: result[k] for k in ('comparability', 'config_delta')}, indent=2))
    print(json.dumps({'candidate_status': new['status'], 'candidate_timing': new.get('timing'),
                      'candidate_seconds': new.get('seconds')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison_directory', type=Path)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    compare(args.root.resolve(), args.comparison_directory.resolve())
