#!/usr/bin/env python3
"""Summarize completed experiment reports without treating completion as timing closure."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re


def timing(path):
    lines = path.read_text().splitlines()
    for i, line in enumerate(lines):
        if line.strip().startswith('WNS(ns)'):
            for data in lines[i + 1:i + 6]:
                values = data.split()
                if len(values) == 12 and re.fullmatch(r'-?\d+\.\d+', values[0]):
                    return dict(zip(
                        ['wns_ns', 'tns_ns', 'setup_failing_endpoints', 'setup_total_endpoints',
                         'whs_ns', 'ths_ns', 'hold_failing_endpoints', 'hold_total_endpoints',
                         'wpws_ns', 'tpws_ns', 'pulse_failing_endpoints', 'pulse_total_endpoints'],
                        [float(x) if j % 4 < 2 else int(x) for j, x in enumerate(values)]))
    raise ValueError('Timing summary table not found: ' + str(path))


def drc(path):
    rules = {}
    for line in path.read_text().splitlines():
        if rules and line.startswith('2. REPORT DETAILS'):
            break
        columns = [part.strip() for part in line.split('|')]
        if len(columns) == 6 and re.fullmatch(r'[A-Z][A-Z0-9_-]*-\d+', columns[1]) and columns[4].isdigit():
            rules[columns[1]] = {'severity': columns[2], 'count': int(columns[4])}
    totals = collections.Counter()
    for entry in rules.values():
        totals[entry['severity']] += entry['count']
    return {'rules': rules, 'totals': dict(totals)}


def summarize(root):
    reports = root / 'reports'
    manifest = json.loads((root / 'manifest.json').read_text())
    status = json.loads((root / 'status.json').read_text())
    result = {'run_directory': str(root), 'status': status,
              'stages': [{k: entry[k] for k in ['stage', 'started', 'finished', 'elapsed_seconds', 'exit_code'] if k in entry}
                         for entry in manifest['stages']],
              'input_validation': json.loads((reports / 'input_validation.json').read_text())}
    for name, fn in [('timing_summary.rpt', timing), ('reference_timing_summary.rpt', timing),
                     ('drc.rpt', drc), ('reference_drc.rpt', drc)]:
        if (reports / name).exists():
            result[name] = fn(reports / name)
    if 'drc.rpt' in result and 'reference_drc.rpt' in result:
        current, reference = result['drc.rpt']['rules'], result['reference_drc.rpt']['rules']
        result['drc_count_changes'] = {rule: current.get(rule, {}).get('count', 0) - reference.get(rule, {}).get('count', 0)
                                       for rule in current.keys() | reference.keys()
                                       if current.get(rule) != reference.get(rule)}
    if (reports / 'route_status.rpt').exists():
        result['routing_counts'] = {}
        for line in (reports / 'route_status.rpt').read_text().splitlines():
            match = re.search(r'# of\s+(.+?)\s*:\s*(\d+)', line)
            if match:
                result['routing_counts'][match[1].rstrip('. ')] = int(match[2])
    if (reports / 'bus_skew.rpt').exists():
        content = (reports / 'bus_skew.rpt').read_text()
        summary = content.rsplit('1. Bus Skew Report Summary', 1)[-1].split('2. Bus Skew Report Per Constraint', 1)[0]
        rows = re.findall(r'^\s*(Slow|Fast)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s+(-?\d+\.\d+)\s*$', summary, re.M)
        if not rows:
            raise ValueError('Bus skew summary has no numeric rows')
        result['bus_skew'] = {'summary_rows': len(rows), 'violations': sum(float(row[3]) < 0 for row in rows),
                              'worst_slack_ns': min(float(row[3]) for row in rows)}
    for name in ['placement_audit.json', 'placement_audit_status.json', 'placement_request_summary.json']:
        if (reports / name).exists():
            result[name] = json.loads((reports / name).read_text())
    log = root / 'logs/03_vivado_route.log'
    if log.exists():
        error_counts = collections.Counter()
        flow_markers = []
        for line in log.open(errors='replace'):
            match = re.match(r'ERROR:\s+\[([^]]+)\]', line)
            if match:
                error_counts[match[1]] += 1
            if line.startswith('FLOW_'):
                flow_markers.append(line.strip())
        result['backend_error_message_counts'] = dict(error_counts)
        result['flow_markers'] = flow_markers
    placement_errors = root / 'placement/placementError'
    if placement_errors.exists():
        result['unresolved_initial_placement_error_file_bytes'] = placement_errors.stat().st_size
        result['unresolved_initial_placement_error_nonempty_lines'] = sum(bool(line.strip()) for line in placement_errors.open())
    result['artifacts'] = {}
    for name in ['faceDetect_amf_routed.dcp', 'route_status.rpt', 'drc.rpt', 'timing_summary.rpt', 'bus_skew.rpt', 'utilization.rpt']:
        path = reports / name
        if path.exists():
            result['artifacts'][name] = {'bytes': path.stat().st_size,
                                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('run_directory', type=Path)
    args = parser.parse_args()
    summary = summarize(args.run_directory)
    output = json.dumps(summary, indent=2, ensure_ascii=False) + '\n'
    (args.run_directory / 'reports/verification_summary.json').write_text(output)
    print(output)
