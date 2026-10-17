"""Hash and verify an empty-all-RPs static input separately from the full reference."""
import csv
import json
from pathlib import Path
from run_dfx import sha, save

SCHEMA = 'amf-common-static-shell-v1'
PARTITION_KEYS = ('id', 'active_partition', 'pblock', 'pblock_ranges', 'expected_slr')


def signature(job):
    return dict(part=job['part'], clock_name=job['clock_name'],
                clock_period_ns=job['clock_period_ns'],
                static_routing_containment=job['static_routing_containment'],
                partitions=[{k:rp.get(k) for k in PARTITION_KEYS} for rp in job['partitions']])


def artifact_names(job):
    return ['common_static_locked.dcp', 'common_static_emptiness.tsv',
            'common_static_route_status.rpt', 'common_static_shell.json'] + [
                rp['id']+'_common_static_boundary.tcldata' for rp in job['partitions']]


def check_empty_proof(job, out):
    out = Path(out)
    with (out/'common_static_emptiness.tsv').open() as f:
        rows = list(csv.reader(f, delimiter='\t'))
    expected = {(phase, rp['id'], rp['active_partition'])
                for phase in ('locked', 'reopened') for rp in job['partitions']}
    found = set()
    for row in rows:
        if len(row) != 6 or tuple(row[:3]) in found:
            raise ValueError('Malformed or duplicate common static emptiness proof')
        found.add(tuple(row[:3]))
        if row[3:5] != ['1', '0'] or int(row[5]) <= 0:
            raise ValueError('Common static input contains a dynamic implementation or lacks its interface')
    if found != expected:
        raise ValueError('Common static proof does not cover every RP before and after DCP reopen')
    times = [line.split('\t') for line in (out/'stage_times.tsv').read_text().splitlines()]
    for stage in ('common_static_remove_all_rms', 'common_static_lock_routing',
                  'common_static_write', 'common_static_validate_reopened'):
        matches = [row for row in times if row[0] == stage]
        if len(matches) != 1 or len(matches[0]) != 3 or matches[0][2] != '0':
            raise ValueError('Missing successful common static stage: '+stage)


def record(job, out, reference):
    out = Path(out)
    check_empty_proof(job, out)
    names = [n for n in artifact_names(job) if n != 'common_static_shell.json']
    names += ['stage_times.tsv', 'stage_intervals.tsv']
    files = {}
    for name in names:
        p = out/name
        if not p.is_file() or not p.stat().st_size:
            raise ValueError('Missing common static artifact: '+name)
        files[name] = sha(p)
    contract = dict(schema=SCHEMA, state='verified_empty_rps_after_reopen',
                    design=signature(job), files=files, routing_locked=True,
                    full_reference_sha256=sha(reference),
                    old_rm_implementations_retained=False,
                    static_route_preservation_note='Static routing locked during production; final filled design still requires pr_verify -full_check against full reference')
    save(out/'common_static_shell.json', contract)
    return contract


def verify(job, out, reference):
    out = Path(out)
    contract = json.loads((out/'common_static_shell.json').read_text())
    if (contract.get('schema') != SCHEMA or contract.get('state') != 'verified_empty_rps_after_reopen'
            or contract.get('design') != signature(job) or contract.get('routing_locked') is not True
            or contract.get('old_rm_implementations_retained') is not False):
        raise ValueError('Incompatible common static shell contract')
    if contract['full_reference_sha256'] != sha(reference):
        raise ValueError('Common static shell belongs to another full reference')
    required = set(artifact_names(job)) - {'common_static_shell.json'}
    required.update(('stage_times.tsv', 'stage_intervals.tsv'))
    if set(contract['files']) != required:
        raise ValueError('Incomplete common static shell hash coverage')
    for name, expected in contract['files'].items():
        if sha(out/name) != expected:
            raise ValueError('Common static artifact changed: '+name)
    check_empty_proof(job, out)
    return contract
