#!/usr/bin/env python3
"""Read existing AMF logs; never run placement, routing, or change run artifacts."""
import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path


def analyze(run):
    manifest_path = run / 'manifest.json'
    log_path = run / 'logs/amf.log'
    manifest = json.loads(manifest_path.read_text())
    process = next(s['elapsed_seconds'] for s in manifest['stages'] if s['name'] == 'amf')
    lines = re.sub(r'\x1b\[[0-9;]*m', '', log_path.read_text()).splitlines()
    markers = {
        'cluster_start': 'STATUS:   Cluster Placement Start.',
        'cluster_end': 'STATUS:   Cluster Placement Done.',
        'packer_initialized': 'ParallelCLBPacker: initialized.',
        'packing_iterations_end': 'ParallelCLBPacker: finish iterative packing',
        'exception_start': 'ParallelCLBPacker: start exceptionHandling.',
        'exception_end': 'ParallelCLBPacker::exceptionHandling done!',
        'export_start': 'ParallelCLBPacker: dumping CLBPacking archieve',
        'placement_done': 'STATUS:   Placement Done',
    }
    timestamps = {}
    owner = None
    pass_index = -1
    qp = spread = None
    passes, matches, legal_intervals = [], [], []
    for line_no, line in enumerate(lines, 1):
        tm = re.search(r'elapsed time: ([\d.]+) s', line)
        t = float(tm[1]) if tm else None
        for key, pattern in markers.items():
            if pattern in line:
                assert key not in timestamps, (key, line_no)
                timestamps[key] = {'seconds': t, 'line': line_no}
        if 'GlobalPlacement_CLBElements started' in line:
            pass_index += 1
            passes.append({'start_s': t, 'qp_iterations': 0, 'legal_iterations': 0,
                           'spreading_interval_s': 0.0, 'legalization_interval_s': 0.0,
                           'matching_s': 0.0})
        if 'MCLB Average Displacement Of Rough Legalization =' in line or 'mCLBLegalizer: Launch.' in line:
            owner = 'mclb'
        if 'MacroLegalizer[BRAMDSPLegalizer] Started' in line:
            owner = 'bram_dsp'
        if 'MacroLegalizer[CARRYMacroLegalizer] Started' in line:
            owner = 'carry'
        if 'MacroLegalizer[' in line and 'Finished Legalization' in line:
            owner = None
        m = re.search(r'AMF_MATCHER backend=(\S+) left=(\d+) right=(\d+) solve_s=([\d.]+)', line)
        if m:
            assert owner and pass_index >= 0, (line_no, line)
            assert m[1] == 'legacy'
            row = {'line': line_no, 'owner': owner, 'pass': pass_index + 1,
                   'left': int(m[2]), 'right': int(m[3]), 'seconds': float(m[4])}
            matches.append(row)
            passes[pass_index]['matching_s'] += row['seconds']
        if pass_index >= 0 and 'packer_initialized' not in timestamps:
            if 'WLOptimizer Iteration#' in line:
                qp = t
                passes[pass_index]['qp_iterations'] += 1
            if 'Spreader Iteration#' in line:
                assert qp is not None
                passes[pass_index]['spreading_interval_s'] += t - qp
                spread = (t, line_no)
                qp = None
            if 'Legalization Iteration#' in line:
                assert spread is not None
                passes[pass_index]['legalization_interval_s'] += t - spread[0]
                passes[pass_index]['legal_iterations'] += 1
                legal_intervals.append({'first_line': spread[1], 'last_line': line_no,
                                        'seconds': t - spread[0]})
                spread = None
    assert len(timestamps) == len(markers)
    for row in matches:
        assert sum(i['first_line'] < row['line'] < i['last_line'] for i in legal_intervals) == 1
    for interval in legal_intervals:
        contained = sum(m['seconds'] for m in matches if interval['first_line'] < m['line'] < interval['last_line'])
        assert contained <= interval['seconds'] + .002
    ts = {k: v['seconds'] for k, v in timestamps.items()}
    spreading = sum(p['spreading_interval_s'] for p in passes)
    legalization = sum(p['legalization_interval_s'] for p in passes)
    global_total = ts['packer_initialized'] - ts['cluster_end']
    decomposition = {
        'pre_cluster_logged': ts['cluster_start'],
        'cluster_initialization': ts['cluster_end'] - ts['cluster_start'],
        'global_other_qp_sta_io_and_transition': global_total - spreading - legalization,
        'global_spreading_intervals': spreading,
        'global_macro_legalization_intervals': legalization,
        'final_packing_and_refinement_before_export': ts['export_start'] - ts['packer_initialized'],
        'output_cleanup_and_timer_origin_residual': process - ts['export_start'],
    }
    assert abs(sum(decomposition.values()) - process) < 1e-7
    by_owner = {}
    for kind in ('mclb', 'bram_dsp', 'carry'):
        rows = [m for m in matches if m['owner'] == kind]
        by_owner[kind] = {'calls': len(rows), 'seconds': sum(m['seconds'] for m in rows)}
    full = [m for m in matches if m['left'] == 12231]
    assert all(m['owner'] == 'mclb' for m in full)
    first_bram = next(i for i, line in enumerate(lines, 1) if 'MacroLegalizer[BRAMDSPLegalizer] Started' in line)
    initial = [m for m in matches if m['line'] < first_bram]
    assert all(m['owner'] == 'mclb' for m in initial)
    first_rip = next(line for line in lines if 'starting parallel ripping up for' in line)
    profile_path = run / 'reports/amf_profile.tsv'
    profile_check = None
    if profile_path.exists():
        with profile_path.open() as f:
            rows = [r for r in csv.DictReader(f, delimiter='\t') if 'MinCostBipartiteMatcher::solve()' in r['function']]
        profile_check = {'calls': sum(int(r['calls']) for r in rows),
                         'inclusive_wall_s': sum(float(r['inclusive_wall_s']) for r in rows)}
    total_match = sum(m['seconds'] for m in matches)
    return {
        'run': str(run), 'binary_sha256': manifest['binary_sha256'],
        'runtime_profiling': manifest.get('runtime_profiling'),
        'input_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (log_path, manifest_path)},
        'process_s': process, 'timestamps': timestamps, 'decomposition_s': decomposition,
        'global_interval_s': global_total, 'passes': passes,
        'matching': {'calls': len(matches), 'seconds': total_match, 'by_owner': by_owner,
                     'full_12231': {'calls': len(full), 'seconds': sum(m['seconds'] for m in full),
                                    'mean_right_nodes': sum(m['right'] for m in full) / len(full)},
                     'initial_mclb': initial, 'initial_mclb_s': sum(m['seconds'] for m in initial),
                     'all_calls': matches, 'baseline_profile_crosscheck': profile_check},
        'process_minus_matching_s': process - total_match,
        'legalization_minus_matching_s': legalization - total_match,
        'final_packing_detail_s': {
            'iterative_packing': ts['packing_iterations_end'] - ts['packer_initialized'],
            'pre_exception_transition': ts['exception_start'] - ts['packing_iterations_end'],
            'exception_handling': ts['exception_end'] - ts['exception_start'],
            'post_exception_refinement_sta_slot_mapping': ts['export_start'] - ts['exception_end'],
        },
        'first_rip_up_pus': int(re.search(r'ripping up for (\d+) PUs', first_rip)[1]),
        'limitations': ['Log marker intervals include intervening diagnostics and I/O.',
                       'Output residual includes the difference between process and logger time origins.',
                       'Legacy solve_s excludes graph construction; includes OpenMP join and result extraction.',
                       'No matching graph/worker timing snapshots; host contention cannot be quantified.',
                       'Profiling differs between runs; not a repeated same-binary causal experiment.'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--external', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = {'schema': 'amf-external-floorplan-runtime-v1',
              'baseline': analyze(args.baseline), 'external': analyze(args.external)}
    result['delta_s'] = {k: result['external']['decomposition_s'][k] - v
                         for k, v in result['baseline']['decomposition_s'].items()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({key: {k: v for k, v in result[key].items()
                            if k in ('process_s', 'decomposition_s', 'final_packing_detail_s', 'first_rip_up_pus')}
                      for key in ('baseline', 'external')}, indent=2))


if __name__ == '__main__':
    main()
