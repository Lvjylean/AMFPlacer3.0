#!/usr/bin/env python3
"""Replay only partition/initial floorplan on immutable full inputs; no Vivado."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def main():
    a = argparse.ArgumentParser()
    a.add_argument('--binary', type=Path, required=True)
    a.add_argument('--full-run', type=Path, required=True)
    a.add_argument('--output', type=Path, required=True)
    a.add_argument('--ordinary', action='store_true')
    a = a.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cfg = json.loads((a.full_run / 'amf_config.json').read_text())
    for k in list(cfg):
        if k.startswith('Native ') or k.startswith('Dump') or k.startswith('DFX '):
            cfg.pop(k)
    cfg.update(dumpDirectory=str(out / 'placement'), BoundaryReportDirectory=str(out / 'physical'))
    cfg['Dump Cluster file'] = 'clusters'
    if a.ordinary:
        cfg['IncremenralCompilation'] = 'false'
    else:
        raw = a.full_run / 'hard_region_sites.before_policy.tsv'
        if not raw.is_file():
            raw = a.full_run / 'hard_region_sites.tsv'
        cfg.update({
            'IncremenralCompilation': 'true', 'DFX placement mode': 'full',
            'DFX partition floorplan': 'true',
            'DFX regions file': str(a.full_run / 'hard_regions.tsv'),
            'DFX region sites file': str(raw),
            'DFX resource inventory file': str(a.full_run / 'resources.tsv'),
            'DFX resource geometry file': str(a.full_run / 'resource_regions.tsv'),
            'DFX effective sites file': str(out / 'effective.tsv'),
            'DFX boundary audit file': str(out / 'boundary.tsv'),
            'DFX partition audit file': str(out / 'partition.tsv'),
        })
    inputs = {v: sha(v) for k, v in cfg.items() if ('file' in k or k == 'mergedSharedCellType2sharedCellType')
              and Path(v).is_file()}
    (out / 'config.json').write_text(json.dumps(cfg, indent=2))
    m = dict(state='running', scope='partition_and_initial_floorplan_only',
             full_placement_executed=False, routing_executed=False,
             binary_sha256=sha(a.binary), inputs=inputs, ordinary=a.ordinary, started_epoch=time.time())
    def save():
        (out / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
    save()
    start = time.monotonic()
    with (out / 'amf.log').open('w') as log:
        proc = subprocess.Popen([str(a.binary.resolve()), str(out/'config.json'),
                                 '--inspect-initial-placement', str(out/'initial.json')],
                                cwd=out, stdout=log, stderr=subprocess.STDOUT,
                                env=dict(os.environ, AMF_PROFILE_OUTPUT=str(out/'profile.tsv')))
        m['pid'] = proc.pid
        save()
        m['exit_code'] = proc.wait()
    m['seconds'] = time.monotonic() - start
    m['inputs_unchanged'] = all(sha(path) == value for path, value in inputs.items())
    m['state'] = 'failed'
    save()
    if m['exit_code'] != 0 or not m['inputs_unchanged']:
        raise RuntimeError('Initialization replay failed; preserve output')
    m['initial'] = json.loads((out/'initial.json').read_text())
    if not a.ordinary:
        with (out/'partition.tsv').open() as f:
            rows = list(csv.DictReader(f, delimiter='\t'))
        with (a.full_run/'hard_regions.tsv').open() as f:
            regions = {r['region']: r['instance'] for r in csv.DictReader(f, delimiter='\t')}
        clusters = [line.split() for line in (out/'placement/clusters').read_text().splitlines()]
        assert len(clusters) == len(rows)
        for row, cells in zip(rows, clusters):
            for name in cells:
                owner = next((region for region, prefix in regions.items()
                              if prefix != '*' and (name == prefix or name.startswith(prefix + '/'))), 'static')
                assert owner == row['region'], (name, owner, row)
        for region in regions:
            if region != 'static':
                assert sum(row['region'] == region for row in rows) == 1
        m['regions'] = rows
        m['cluster_cells_including_virtual'] = {row['region']: len(cells) for row, cells in zip(rows, clusters) if row['region'] != 'static'}
        m['cluster_pus'] = sum(int(row['placement_units']) for row in rows)
        assert m['cluster_pus'] == m['initial']['placement_units']
        with (a.full_run/'cells.tsv').open() as f:
            expected = {r['cell']: r['region'] for r in csv.DictReader(f, delimiter='\t')}
        seen, real_counts = set(), {}
        for row, names in zip(rows, clusters):
            for name in names:
                if name in expected:
                    assert expected[name] == row['region'] and name not in seen
                    seen.add(name)
                    real_counts[row['region']] = real_counts.get(row['region'], 0) + 1
        assert seen == set(expected)
        m['real_cells_by_region'] = real_counts
        m['partition_membership_verified'] = True
    m['state'] = 'completed'
    save()
    print(json.dumps(m, indent=2))

if __name__ == '__main__':
    main()
