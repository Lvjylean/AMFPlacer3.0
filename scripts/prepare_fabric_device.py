#!/usr/bin/env python3
"""Convert audited Vivado fabric sites into AMF's device input (format v2).

X preserves relative RPM column spacing, normalized by the common slice pitch.
Y uses slice rows per clock region and evenly spaced resource rows within it.
These are placement metric coordinates, not a calibrated wire-delay model.
The original RPM coordinates remain in sites.tsv for independent auditing.
"""
import argparse
from collections import Counter, defaultdict
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


def convert(source, destination, part, metadata=None):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        raise ValueError('Destination already exists: ' + str(destination))
    exported = None
    if metadata is not None:
        exported = dict(line.rstrip('\n').split('\t', 1) for line in Path(metadata).read_text().splitlines())
        if exported.get('part') != part or exported.get('scope') != 'fabric-sites':
            raise ValueError('Device metadata part/scope mismatch')
    with source.open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    if not rows:
        raise ValueError('Empty device')
    names, groups, cr_slr = set(), defaultdict(list), {}
    for row in rows:
        name = row['site']
        if name in names:
            raise ValueError('Duplicate site: ' + name)
        names.add(name)
        site = re.fullmatch(r'(SLICE|DSP48E2|RAMB18|RAMB36|URAM288)_X(\d+)Y(\d+)', name)
        cr = re.fullmatch(r'X(\d+)Y(\d+)', row['clock_region'])
        if not site or not cr:
            raise ValueError('Invalid site or clock region: ' + name)
        row['family'], row['site_x'], row['site_y'] = site[1], int(site[2]), int(site[3])
        row['cr_y'] = int(cr[2])
        row['slr'], row['prohibited'] = int(row['slr']), int(row['prohibited'])
        if row['slr'] < 0 or row['prohibited'] not in (0, 1):
            raise ValueError('Invalid SLR/availability: ' + name)
        if cr_slr.setdefault(row['clock_region'], row['slr']) != row['slr']:
            raise ValueError('Clock region belongs to multiple SLRs')
        row['rpm_x'], row['rpm_y'] = float(row['rpm_x']), float(row['rpm_y'])
        bels = row['bels'].split(',')
        if not bels or any(not b.startswith(name + '/') or any(c.isspace() for c in b) for b in bels):
            raise ValueError('Invalid BEL names: ' + name)
        groups[(row['clock_region'], row['family'], row['site_x'])].append(row)
    heights = {len(g) for (_, family, _), g in groups.items() if family == 'SLICE'}
    if len(heights) != 1:
        raise ValueError('Inconsistent slice-column height; explicit geometry mapping required')
    height = next(iter(heights))
    if not height:
        raise ValueError('No slice rows')
    slice_x = sorted({r['rpm_x'] for r in rows if r['family'] == 'SLICE'})
    pitches = Counter(b - a for a, b in zip(slice_x, slice_x[1:]) if b > a)
    if not pitches:
        raise ValueError('Need at least two slice columns to determine X pitch')
    x_pitch = pitches.most_common(1)[0][0]
    min_x = min(r['rpm_x'] for r in rows)
    for group in groups.values():
        group.sort(key=lambda r: r['site_y'])
        if any(b['site_y'] != a['site_y'] + 1 for a, b in zip(group, group[1:])):
            raise ValueError('Non-contiguous site rows; explicit geometry mapping required')
        for i, row in enumerate(group):
            row['x'] = (row['rpm_x'] - min_x) / x_pitch
            row['y'] = row['cr_y'] * height + i * height / len(group)
    counts, prohibited = defaultdict(Counter), defaultdict(Counter)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        with z.open('exportSiteLocation', 'w') as f:
            for row in sorted(rows, key=lambda r: r['site']):
                counts[row['slr']][row['site_type']] += 1
                if row['prohibited']:
                    prohibited[row['slr']][row['site_type']] += 1
                line = (f"site=> {row['site']} tile=> {row['tile']} clockRegionName=> {row['clock_region']} "
                        f"sitetype=> {row['site_type']} tiletype=> {row['tile_type']} "
                        f"centerx=> {row['x']:.8f} centery=> {row['y']:.8f} BELs=> [{row['bels']}] "
                        f"slr=> {row['slr']} prohibited=> {row['prohibited']}\n")
                f.write(line.encode())
    manifest = dict(schema='amf-fabric-device-v2', part=part, scope='fabric-sites',
                    source_sha256=digest(source), device_sha256=digest(destination),
                    coordinate_model='normalized-rpm-x-clock-region-rows-v1',
                    x_pitch=x_pitch, slice_rows_per_clock_region=height,
                    site_count=len(rows), slr_count=len(counts), sites_by_slr=dict(counts),
                    unavailable_by_slr=dict(prohibited), clock_region_slr=cr_slr)
    anchors = {}
    for row in rows:
        if row['family'] == 'SLICE':
            old = anchors.setdefault(row['rpm_y'], row['y'])
            if abs(old - row['y']) > 1e-6:
                raise ValueError('Ambiguous RPM Y to AMF Y mapping')
    anchor_rows = sorted(anchors.items())
    if any(b[1] <= a[1] for a, b in zip(anchor_rows, anchor_rows[1:])):
        raise ValueError('Non-monotone RPM Y mapping')
    mapping = dict(schema='amf-coordinate-map-v1', part=part,
                   model=manifest['coordinate_model'], rpm_x_origin=min_x,
                   rpm_x_pitch=x_pitch, rpm_y_anchors=anchor_rows,
                   slice_rows_per_clock_region=height)
    mapping_path = destination.with_suffix('.coordinates.json')
    mapping_path.write_text(json.dumps(mapping, indent=2) + '\n')
    manifest['coordinate_map_sha256'] = digest(mapping_path)
    if metadata is not None:
        manifest['export_metadata'] = exported
        manifest['metadata_sha256'] = digest(metadata)
    destination.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('sites', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--part', required=True)
    p.add_argument('--metadata', required=True, type=Path)
    args = p.parse_args()
    print(json.dumps(convert(args.sites, args.output, args.part, args.metadata), indent=2))
