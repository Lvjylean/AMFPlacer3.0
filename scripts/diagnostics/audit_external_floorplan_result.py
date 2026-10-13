#!/usr/bin/env python3
"""Audit external membership against actual routed sites; no placement changes."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import re
import zipfile


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def audit(run):
    cfg = json.loads((run / 'config.json').read_text())
    initial = json.loads((run / 'reports/external_floorplan.json').read_text())
    sites = {}
    cr_slr = {}
    archive = Path(cfg['vivado extracted device information file'])
    with zipfile.ZipFile(archive) as z:
        assert len(z.namelist()) == 1
        with z.open(z.namelist()[0]) as f:
            for raw in f:
                fields = dict(re.findall(r'(\w+)=>\s+(\S+)', raw.decode()))
                site, cr, slr = fields['site'], fields['clockRegionName'], fields['slr']
                assert site not in sites
                assert cr not in cr_slr or cr_slr[cr] == slr
                sites[site] = (cr, slr)
                cr_slr[cr] = slr
    regions_file = Path(cfg['external floorplan regions file'])
    with regions_file.open() as f:
        regions = {r['module']: set(r['clock_regions'].split()) for r in csv.DictReader(f, delimiter='\t')}
    membership_file = Path(cfg['external floorplan membership file'])
    members = {}
    totals = collections.Counter()
    with membership_file.open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            assert row['cell'] not in members
            assert row['module'] in regions
            members[row['cell']] = row['module']
            totals[row['module']] += 1
    assert len(members) == initial['real_cells']
    allowed_slrs = {m: {cr_slr[cr] for cr in crs} for m, crs in regions.items()}
    counts = {m: collections.Counter() for m in regions}
    slrs = {m: collections.Counter() for m in regions}
    crs = {m: collections.Counter() for m in regions}
    seen = set()
    extra = 0
    path = run / 'placement/routed_cell_sites.tsv'
    missing_examples = []
    with path.open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            name, site = row['cell'], row['site']
            if name not in members:
                extra += 1
                continue
            assert name not in seen
            seen.add(name)
            m = members[name]
            if site not in sites:
                counts[m]['unknown_site'] += 1
                if len(missing_examples) < 10:
                    missing_examples.append({'cell': name, 'site': site})
                continue
            cr, slr = sites[site]
            counts[m]['mapped'] += 1
            counts[m]['inside_preferred_crs' if cr in regions[m] else 'outside_preferred_crs'] += 1
            counts[m]['inside_preferred_slrs' if slr in allowed_slrs[m] else 'outside_preferred_slrs'] += 1
            slrs[m][slr] += 1
            crs[m][cr] += 1
    modules = {}
    for m in sorted(regions):
        c = counts[m]
        modules[m] = {'cells': totals[m], 'initially_inside_preferred_crs': initial['module_cells'][m]['initially_inside'],
                      **{k: c[k] for k in ('mapped', 'unknown_site', 'inside_preferred_crs', 'outside_preferred_crs', 'inside_preferred_slrs', 'outside_preferred_slrs')},
                      'preferred_slrs': sorted(allowed_slrs[m]), 'routed_slr_distribution': dict(slrs[m]), 'routed_cr_distribution': dict(crs[m])}
    result = {'schema': 'external-floorplan-result-audit-v1', 'scope': 'Real input cells only. Actual site-to-CR/SLR metadata; region sets remain unions, not bounding boxes. Outside a preferred region is allowed, not a legality violation.',
              'run': str(run), 'input_cells': len(members), 'matched_cells': len(seen), 'missing_cells': len(members.keys() - seen),
              'unknown_sites': sum(c['unknown_site'] for c in counts.values()), 'unknown_site_examples': missing_examples,
              'unrequested_primitives_excluded': extra, 'initially_inside_preferred_crs': sum(m['initially_inside_preferred_crs'] for m in modules.values()),
              'routed_inside_preferred_crs': sum(c['inside_preferred_crs'] for c in counts.values()),
              'routed_outside_preferred_crs': sum(c['outside_preferred_crs'] for c in counts.values()),
              'routed_outside_preferred_slrs': sum(c['outside_preferred_slrs'] for c in counts.values()),
              'routed_slr_distribution': dict(sum(slrs.values(), collections.Counter())), 'modules': modules,
              'input_sha256': {str(p): digest(p) for p in (archive, membership_file, regions_file, path)}}
    if result['missing_cells'] or result['unknown_sites']:
        raise ValueError('Incomplete site/membership coverage: ' + json.dumps(result))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Refusing to overwrite an existing audit')
    result = audit(args.run.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('modules', 'input_sha256')}))
