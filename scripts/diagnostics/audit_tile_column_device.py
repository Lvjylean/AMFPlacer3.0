#!/usr/bin/env python3
"""Audit the generated U250 tile-column device against its frozen RPM predecessor.

This is a geometry/input check, not a placement quality or timing measurement.
Raw exports and ZIP inputs stay on the server; only JSON evidence is small.
"""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_physical_boundaries import read_fabric, validate_model_inputs


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()


def audit(old, new, raw, coordinates, model):
    previous = {r['site']: r for r in read_fabric(old)}
    current = {r['site']: r for r in read_fabric(new)}
    assert previous.keys() == current.keys(), 'Site identities changed'
    # Includes tile names, CR, BELs and availability as well as Y coordinates.
    def without_x(path):
        with zipfile.ZipFile(path) as z:
            return [re.sub(rb'centerx=> \S+', b'centerx=> <X>', line)
                    for line in z.read('exportSiteLocation').splitlines()]
    assert without_x(old) == without_x(new), 'Non-X device content changed'
    with Path(raw).open() as f: rows = list(csv.DictReader(f, delimiter='\t'))
    assert len(rows) == len(current)
    columns = defaultdict(set)
    for r in rows:
        tx = int(re.search(r'_X(\d+)Y\d+$', r['tile'])[1])
        columns[tx].add((float(r['rpm_x']), r['tile_type']))
    type_sides = {}
    for column in columns.values():
        positions = sorted({p[0] for p in column})
        if len(positions) == 2:
            for rpm, typ in column:
                value = -.25 if rpm == positions[0] else .25
                assert type_sides.setdefault(typ, value) == value
    mapping = json.loads(Path(coordinates).read_text())
    cuts = [e['before_tile_x'] for e in mapping['special_columns']]
    max_error = 0
    for r in rows:
        tx = int(re.search(r'_X(\d+)Y\d+$', r['tile'])[1])
        x = tx + sum(tx >= cut for cut in cuts) + type_sides[r['tile_type']]
        max_error = max(max_error, abs(x - current[r['site']]['x']))
    assert max_error < 1e-8, 'Column/subslot formula mismatch'
    report = validate_model_inputs(model, new)
    by_cr = defaultdict(list)
    for r in current.values(): by_cr[r['clock_region']].append(r)
    cr_columns = []
    for col in range(8):
        ranges = sorted({(min(r['x'] for r in v), max(r['x'] for r in v))
                         for cr,v in by_cr.items() if cr.startswith(f'X{col}Y')})
        cr_columns.append(dict(cr_x=col, fabric_ranges=ranges,
                               spans=[b-a for a,b in ranges]))
    changed = sum(previous[n]['x'] != r['x'] for n,r in current.items())
    return dict(schema='amf-tile-column-audit-v1', site_count=len(current),
        changed_x_site_count=changed, unchanged_non_x_content=True,
        max_formula_error=max_error, tile_column_count=len(columns), type_subslots=type_sides,
        special_columns=mapping['special_columns'], clock_region_columns=cr_columns,
        old_x_range=[min(r['x'] for r in previous.values()), max(r['x'] for r in previous.values())],
        new_x_range=[min(r['x'] for r in current.values()), max(r['x'] for r in current.values())],
        y_range=[min(r['y'] for r in current.values()), max(r['y'] for r in current.values())],
        physical_boundaries=report['boundaries'],
        source_sha256={k:sha(p) for k,p in [('old',old),('new',new),('raw',raw),
            ('coordinates',coordinates),('model',model)]},
        scope='Geometry and format audit; no full placement, route or timing assertion')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('old','new','raw','coordinates','model','output'):
        p.add_argument('--'+key, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists(): raise ValueError('Refusing to overwrite audit evidence')
    result = audit(a.old, a.new, a.raw, a.coordinates, a.model)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('site_count','changed_x_site_count',
        'unchanged_non_x_content','max_formula_error','old_x_range','new_x_range',
        'clock_region_columns')}, indent=2))
