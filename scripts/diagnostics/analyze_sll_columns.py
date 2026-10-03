#!/usr/bin/env python3
"""Reconcile per-column routed SLL occupancy against native Vivado totals."""
import argparse
import csv
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path


def analyze(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('exit_code') != 0:
        raise ValueError('SLL export has not completed successfully')
    columns = list(csv.DictReader((directory / 'sll_columns.tsv').open(), delimiter='\t'))
    nodes = defaultdict(lambda: [0, 0, 0])
    seen = set()
    for row in csv.DictReader((directory / 'sll_nodes.tsv').open(), delimiter='\t'):
        key = row['boundary'], int(row['column'])
        identity = row['boundary'], row['node']
        if identity in seen:
            raise ValueError(f'Duplicate node: {identity}')
        seen.add(identity)
        used, bad = int(row['used']), int(row['bad'])
        if used not in (0, 1) or bad not in (0, 1):
            raise ValueError('Invalid Boolean node status')
        nodes[key][0] += 1
        nodes[key][1] += used
        nodes[key][2] += bad
    buckets = {}
    totals = defaultdict(lambda: [0, 0])
    for row in columns:
        key = row['boundary'], int(row['column'])
        if key in buckets:
            raise ValueError(f'Duplicate column: {key}')
        cap, used, bad = (int(row[f]) for f in ['physical_capacity', 'routed_used', 'bad_nodes'])
        if nodes[key] != [cap, used, bad] or not 0 <= used <= cap:
            raise ValueError(f'Node/column mismatch: {key}')
        if abs(float(row['routed_utilization']) - used / cap) > 1e-8:
            raise ValueError(f'Utilization mismatch: {key}')
        buckets[key] = dict(boundary=key[0], column=key[1], physical_capacity=cap,
                            routed_used=used, bad_nodes=bad, utilization=used / cap)
        totals[key[0]][0] += cap
        totals[key[0]][1] += used
    if set(nodes) != set(buckets):
        raise ValueError('Node and column bucket sets differ')
    native = {}
    report = (directory / 'utilization.rpt').read_text()
    for a, b, used, cap in re.findall(
            r'\|\s*(SLR\d+)\s*<->\s*(SLR\d+)\s*\|\s*(\d+)\s*\|[^|]*\|\s*(\d+)\s*\|', report):
        boundary = ':'.join(sorted([a, b], key=lambda x: int(x[3:])))
        native[boundary] = [int(cap), int(used)]
    native_total = re.search(r'\|\s*Total SLLs Used\s*\|\s*(\d+)', report)
    if dict(totals) != native or native_total is None:
        raise ValueError(f'Native boundary mismatch: {dict(totals)} != {native}')
    if sum(v[1] for v in totals.values()) != int(native_total.group(1)):
        raise ValueError('Native total mismatch')
    results = []
    for boundary, (cap, used) in sorted(totals.items()):
        group = [r for r in buckets.values() if r['boundary'] == boundary]
        results.append(dict(boundary=boundary, physical_capacity=cap, routed_used=used,
                            utilization=used / cap, columns=len(group),
                            peak=max(group, key=lambda x: x['utilization'])))
    hashes = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
              for name in ['sll_nodes.tsv', 'sll_columns.tsv', 'sll_boundaries.tsv', 'utilization.rpt']}
    return dict(schema='sll-column-audit-v1', native_reconciliation_passed=True,
                scope='Post-route physical SLL node occupancy; not estimated congestion demand.',
                input_dcp=manifest['input_dcp'], input_sha256=manifest['input_sha256'],
                column_definition='Vivado tile COLUMN; each node endpoints verified to share this column',
                physical_sll_nodes=len(seen), total_routed_used=int(native_total.group(1)),
                boundaries=results, top_columns=sorted(buckets.values(), key=lambda x: -x['utilization'])[:10],
                hashes=hashes)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    (args.directory / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
