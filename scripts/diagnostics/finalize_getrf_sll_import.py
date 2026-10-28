#!/usr/bin/env python3
"""Verify the frozen GETRF import experiment; never route or download its DCP."""
import collections
import csv
import hashlib
import json
from pathlib import Path
import sys
import zipfile


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    run = Path(sys.argv[1]).resolve()
    reports = run / 'reports'
    status = json.loads((run / 'status.json').read_text())
    assert status['state'] == 'completed' and status['exit_code'] == 0, status
    def rows(name):
        with (reports / name).open() as f:
            return list(csv.DictReader(f, delimiter='\t'))
    def pairs(name):
        return dict(line.split('\t', 1) for line in (reports / name).read_text().splitlines())
    app = rows('constraint_application.tsv')
    selection = rows('sll-source-selection.tsv')
    assert len(app) == len(selection) == 8442
    assert len({r['net'] for r in app}) == 8442
    assert {r['net'] for r in app} == {r['net'] for r in selection}
    assert sum(int(r['crossings']) for r in app) == 8467
    assert sum(int(r['receivers']) for r in app) == 280438
    assert all(r['fixed_route_nonempty'] == '1' for r in app)
    assert all(r['import_selected_outputs'] for r in selection)
    assert pairs('placement_only_summary.tsv')['routing_executed'] == '0'
    assert int(pairs('checkpoint_constraint_readback.tsv')['verified_fixed_route_nets']) == 8442
    placed = json.loads((reports / 'placed_placement.json').read_text())
    readback = json.loads((reports / 'readback_placement.json').read_text())
    for value in (placed, readback):
        for key in ('requested', 'present', 'placed', 'exact_loc_bel_matches', 'primary_bel_matches'):
            assert value[key] == 856998, (key, value[key])
    assert placed == readback
    manifest = json.loads((run / 'manifest.json').read_text())
    for path, expected in manifest['inputs'].items():
        assert sha(path) == expected, ('Input changed', path)
    checkpoint = reports / 'getrf_placed.dcp'
    before = checkpoint.stat()
    with zipfile.ZipFile(checkpoint) as archive:
        assert archive.testzip() is None
        members = len(archive.infolist())
    checksum = sha(checkpoint)
    after = checkpoint.stat()
    assert (before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
    result = {
        'qualification': 'Full constraint import and DCP persistence passed; GETRF routing was not executed.',
        'run': str(run), 'status': status,
        'tools': json.loads((reports / 'tools.json').read_text()), 'clock_period_ns': 10.0,
        'fixed_route_nets': len(app), 'planned_sll_crossings': 8467,
        'logical_receiver_records': 280438, 'checkpoint_fixed_routes_verified': 8442,
        'source_modes': dict(collections.Counter(r['mode'] for r in selection)),
        'multi_output_drivers': sum(len(r['candidate_outputs'].split()) > 1 for r in selection),
        'route_status_after_import': dict(collections.Counter(r['route_status'] for r in app)),
        'placement_before_and_after_identical': True, 'placement_readback': readback,
        'input_hashes_unchanged': True, 'routing_executed': False,
        'stages_seconds': {r['stage']: float(r['seconds']) for r in rows('stages.tsv')},
        'checkpoint': {'path': str(checkpoint), 'sha256': checksum, 'bytes': before.st_size,
                       'zip_members': members, 'crc_passed': True, 'storage': 'server-only'},
    }
    (run / 'validation-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
