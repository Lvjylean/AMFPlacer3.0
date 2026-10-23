#!/usr/bin/env python3
"""Independent placement/routing limits; missing evidence never counts as pass."""
import argparse
import csv
import json
import math
from pathlib import Path


def intervals(path):
    data = {}
    for line in path.read_text().splitlines():
        name, begin, end, rc = line.split('\t')
        if int(rc):
            continue
        data.setdefault(name, []).append((int(begin)/1000, int(end)/1000))
    return data


def span(values):
    return max(b for a, b in values)-min(a for a, b in values)


def check(value, reference):
    if not math.isfinite(value) or not math.isfinite(reference) or value < 0 or reference <= 0:
        raise ValueError('Invalid timing evidence')
    ratio = value/reference
    return dict(seconds=value, reference_seconds=reference, ratio=ratio,
                maximum_ratio=0.70, passed=ratio <= 0.70)


def amf_placement(root, manifest, scope):
    if manifest.get('amf_placement_scope') != scope:
        raise ValueError('Missing AMF placement scope evidence: '+scope)
    stages = [s for s in manifest.get('stages', [])
              if s['name']=='amf' and s.get('exit_code')==0]
    if len(stages) != 1:
        raise ValueError('Missing unambiguous successful AMF process: '+str(root))
    with (root/'amf_profile.tsv').open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    run = sum(float(r['inclusive_wall_s']) for r in rows
              if r['thread_index']=='0' and r['category']=='placement_orchestration')
    export = sum(float(r['self_wall_s']) for r in rows
                 if r['thread_index']=='0' and r['category']=='export_vivado')
    if not math.isfinite(run) or not math.isfinite(export) or run <= 0 or not 0 <= export < run:
        raise ValueError('Invalid AMF placement profile: '+str(root))
    return run-export, export, stages[0]


def evaluate(reference, incremental):
    reference, incremental = Path(reference), Path(incremental)
    full = json.loads((reference/'manifest.json').read_text())
    inc = json.loads((incremental/'manifest.json').read_text())
    if full['state'] != 'completed' or inc['state'] != 'completed':
        raise ValueError('Implementation incomplete or failed; no performance acceptance')
    if not full.get('acceptance', {}).get('implementation_verified') or not inc.get('acceptance', {}).get('implementation_verified'):
        raise ValueError('Both complete-design implementations must pass correctness checks')
    # Bind the reference to the same modified whole design, not a historical
    # native-Vivado run or merely a matching clock/part number.
    full_identity, inc_identity = full.get('comparison_identity'), inc.get('comparison_identity')
    if not isinstance(full_identity, dict) or not isinstance(inc_identity, dict):
        raise ValueError('Missing comparison identity; functional-only runs are not performance references')
    for key in ('v1_design_input_sha256', 'placement_constraints_sha256', 'amf_binary_sha256', 'amf_settings_sha256'):
        a, b = full_identity.get(key), inc_identity.get(key)
        if not a or len(a)!=64 or a != b:
            raise ValueError('Missing or mismatched comparison identity: '+key)
    for key in ('part', 'clock_name', 'clock_period_ns'):
        if full['job'][key] != inc['job'][key]:
            raise ValueError('Reference mismatch: '+key)
    ft = intervals(reference/'stage_intervals.tsv')
    fp, full_export, full_amf = amf_placement(reference, full, 'whole_design')
    fr = sum(b-a for a,b in ft['vivado_route'])
    workers = {}; placement_intervals = []; routing_intervals = []
    for ident in inc['workers'] if 'workers' in inc else ('a','b'):
        root = incremental/'workers'/ident
        m = json.loads((root/'manifest.json').read_text())
        if m['state'] != 'completed' or not m['acceptance']['implementation_verified']:
            raise ValueError('Worker not verified: '+ident)
        if m.get('binary_sha256') != full['comparison_identity']['amf_binary_sha256']:
            raise ValueError('Worker binary mismatch: '+ident)
        placement, export, st = amf_placement(root, m, 'dynamic_only_fixed_static_context')
        placement_intervals.append((st['started_epoch'],st['ended_epoch']))
        wt = intervals(root/'stage_intervals.tsv')
        routing_intervals += wt['vivado_route']
        workers[ident] = dict(placement=check(placement,fp),
                              amf_process_seconds=st['seconds'],export_seconds=export,
                              routing=check(sum(b-a for a,b in wt['vivado_route']),fr),
                              vivado_compatibility_placement_seconds=sum(b-a for a,b in wt.get('vivado_place',[])))
    parallel_p = check(span(placement_intervals),fp)
    parallel_p['scope'] = 'conservative elapsed span of both AMF processes; includes startup/export and any start skew'
    parallel_r = check(span(routing_intervals),fr)
    parallel_r['scope'] = 'elapsed span of route_design intervals; includes start skew, not a sum'
    individual_ok = all(w[x]['passed'] for w in workers.values() for x in ('placement','routing'))
    result = dict(reference=str(reference),incremental=str(incremental),workers=workers,
                  parallel_placement_upper_bound=parallel_p,parallel_routing=parallel_r,
                  full_engine='ordinary Pro AMF whole-design placement + Vivado full routing',
                  incremental_engine='ordinary Pro AMF dynamic placement with fixed static placement + Vivado dynamic routing with locked static routing',
                  full_placement_seconds=fp,full_amf_process_seconds=full_amf['seconds'],full_export_seconds=full_export,
                  full_vivado_compatibility_placement_seconds=sum(b-a for a,b in ft.get('vivado_place',[])),
                  threshold=0.70,
                  correctness_verified=inc['acceptance']['implementation_verified'],
                  timing_met=inc['acceptance'].get('timing_met'),
                  total_wall_seconds=inc.get('wall_seconds'))
    # An AMF process span includes initialization and export; it is an upper
    # bound, not the user's placement metric. If it exceeds the budget, actual
    # placement-phase timestamps are needed before declaring failure/pass.
    primary_placement = inc.get('parallel_amf_placement_timing')
    if primary_placement is not None:
        if primary_placement.get('scope') != 'placement_only_excluding_export_and_vivado':
            raise ValueError('Wrong parallel placement timing scope')
        measured = check(float(primary_placement['seconds']),fp)
        if measured['seconds']+0.001 < max(w['placement']['seconds'] for w in workers.values()):
            raise ValueError('Parallel placement span shorter than a worker placement')
    elif parallel_p['passed']:
        measured = dict(parallel_p,scope='conservative process upper bound proves placement budget')
    else:
        raise ValueError('AMF process upper bound exceeds 70%; actual parallel placement-phase timing is required (startup/export are excluded)')
    result['parallel_placement']=measured
    result['performance_passed'] = individual_ok and measured['passed'] and parallel_r['passed']
    result['status'] = 'passed' if result['performance_passed'] and result['timing_met'] else 'failed'
    result['failure_reasons'] = []
    for ident,w in workers.items():
        for phase in ('placement','routing'):
            if not w[phase]['passed']:
                result['failure_reasons'].append(ident+' '+phase+' exceeds 70%')
    if not measured['passed']:
        result['failure_reasons'].append('Parallel AMF placement exceeds 70%')
    if not parallel_r['passed']:
        result['failure_reasons'].append('Parallel routing span exceeds 70%; inspect routing iterations, fixed static conflicts and start skew')
    if not result['timing_met']:
        result['failure_reasons'].append('Combined 10ns timing not met')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference',required=True);p.add_argument('--incremental',required=True);p.add_argument('--output',required=True)
    a = p.parse_args()
    try:
        result = evaluate(a.reference,a.incremental)
    except (ValueError,KeyError,FileNotFoundError) as exc:
        result = dict(status='not_evaluable',performance_passed=False,reason=str(exc))
    Path(a.output).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
    raise SystemExit(0 if result['status']=='passed' else 2)
