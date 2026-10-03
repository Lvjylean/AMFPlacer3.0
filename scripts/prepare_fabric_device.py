#!/usr/bin/env python3
"""Convert audited Vivado fabric sites into AMF's device input (format v2).

X uses ordered tile-column bases, +/-0.25 subslots and audited special columns.
The previous normalized-RPM mode is explicit, for historical reproduction only.
Y uses actual tile-row anchors and subdivisions of each resource tile.
The exported center fields are placement anchors, not physical site centers.
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
from fabric_x_coordinates import derive_x_map, mapped_x


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def convert(source, destination, part, metadata=None, *, x_model='tile-columns',
            structure_sites=None, structure_tiles=None):
    source, destination = Path(source), Path(destination)
    if any(p.exists() for p in (destination, destination.with_suffix('.coordinates.json'),
                                  destination.with_suffix('.manifest.json'))):
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
        tile = re.search(r'_X(\d+)Y(\d+)$', row['tile'])
        if not tile:
            raise ValueError('Invalid tile coordinate: ' + row['tile'])
        row['tile_y'] = int(tile[2])
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
    if x_model == 'rpm':
        slice_x = sorted({r['rpm_x'] for r in rows if r['family'] == 'SLICE'})
        pitches = Counter(b - a for a, b in zip(slice_x, slice_x[1:]) if b > a)
        if not pitches:
            raise ValueError('Need at least two slice columns to determine X pitch')
        x_pitch = pitches.most_common(1)[0][0]
        min_x = min(r['rpm_x'] for r in rows)
    slice_y = sorted({r['tile_y'] for r in rows if r['family'] == 'SLICE'})
    y_pitches = Counter(b - a for a, b in zip(slice_y, slice_y[1:]))
    if not y_pitches:
        raise ValueError('Need at least two SLICE rows')
    y_pitch = y_pitches.most_common(1)[0][0]
    y_origin = slice_y[0]
    cr_origins = {}
    for (cr, family, _), group in groups.items():
        if family != 'SLICE':
            continue
        tile_rows = sorted(r['tile_y'] for r in group)
        if any(b - a != y_pitch for a, b in zip(tile_rows, tile_rows[1:])):
            raise ValueError('Non-contiguous SLICE tile rows: ' + cr)
        if cr_origins.setdefault(cr, tile_rows[0]) != tile_rows[0]:
            raise ValueError('Inconsistent SLICE tile origin: ' + cr)
    resource_geometry = defaultdict(set)
    for (cr, family, _), group in groups.items():
        group.sort(key=lambda r: r['site_y'])
        if any(b['site_y'] != a['site_y'] + 1 for a, b in zip(group, group[1:])):
            raise ValueError('Non-contiguous site rows; explicit geometry mapping required')
        if len({r['rpm_x'] for r in group}) != 1:
            raise ValueError('Inconsistent RPM X within resource column')
        by_tile = defaultdict(list)
        for row in group:
            by_tile[row['tile_y']].append(row)
        origins = sorted(by_tile)
        if cr not in cr_origins:
            raise ValueError('Resource clock region has no SLICE reference: ' + cr)
        start, end = cr_origins[cr], cr_origins[cr] + height * y_pitch
        # Include the final tile's span to the region boundary. Missing tiles
        # cannot silently enlarge their neighbours or compress the region.
        spans = {b - a for a, b in zip(origins, origins[1:] + [end])}
        counts_per_tile = {len(v) for v in by_tile.values()}
        if origins[0] != start or len(spans) != 1 or min(spans) <= 0 or len(counts_per_tile) != 1:
            raise ValueError('Irregular resource tile coverage: ' + family + ' ' + cr)
        span = spans.pop() / y_pitch
        n = counts_per_tile.pop()
        resource_geometry[family].add((span, n))
        for origin in origins:
            tile_sites = sorted(by_tile[origin], key=lambda r: r['site_y'])
            if any(b['rpm_y'] <= a['rpm_y'] for a, b in zip(tile_sites, tile_sites[1:])):
                raise ValueError('RPM/site order mismatch within tile')
            for i, row in enumerate(tile_sites):
                row['y'] = (origin - y_origin) / y_pitch + i * span / n
    if any(len(v) != 1 for v in resource_geometry.values()):
        raise ValueError('Inconsistent resource tile geometry across columns/regions')
    geometry = {family: dict(tile_span_rows=next(iter(v))[0], sites_per_tile=next(iter(v))[1])
                for family, v in resource_geometry.items()}
    if x_model == 'tile-columns':
        if structure_sites is None or structure_tiles is None:
            raise ValueError('Tile-column coordinates require full structure sites and tiles')
        x_mapping = derive_x_map(rows, structure_sites, structure_tiles)
        coordinate_model = 'tile-columns-subsites-v3'
    elif x_model == 'rpm':
        x_mapping = dict(x_kind='rpm-affine', rpm_x_origin=min_x, rpm_x_pitch=x_pitch)
        coordinate_model = 'normalized-rpm-x-tile-row-anchors-v2'
    else:
        raise ValueError('Unknown X model: ' + x_model)
    anchors = {}
    for row in rows:
        row['x'] = mapped_x(row['rpm_x'], x_mapping)
        if row['family'] == 'SLICE':
            old = anchors.setdefault(row['rpm_y'], row['y'])
            if abs(old - row['y']) > 1e-6:
                raise ValueError('Ambiguous RPM Y to AMF Y mapping')
    anchor_rows = sorted(anchors.items())
    if any(b[1] <= a[1] for a, b in zip(anchor_rows, anchor_rows[1:])):
        raise ValueError('Non-monotone RPM Y mapping')
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
                    coordinate_model=coordinate_model,
                    converter_sha256=digest(Path(__file__)), resource_geometry=geometry,
                    x_helper_sha256=digest(Path(__file__).with_name('fabric_x_coordinates.py')),
                    slice_rows_per_clock_region=height,
                    site_count=len(rows), slr_count=len(counts), sites_by_slr=dict(counts),
                    unavailable_by_slr=dict(prohibited), clock_region_slr=cr_slr)
    if x_model == 'rpm':
        manifest['x_pitch'] = x_pitch
    else:
        manifest['x_sources'] = {name: dict(path=str(Path(p).resolve()), sha256=digest(p))
            for name, p in [('structure_sites', structure_sites), ('structure_tiles', structure_tiles)]}
        manifest['tile_column_count'] = len(x_mapping['tile_columns'])
        manifest['special_column_count'] = len(x_mapping['special_columns'])
    mapping = dict(schema='amf-coordinate-map-v2', part=part,
                   model=manifest['coordinate_model'], **x_mapping, rpm_y_anchors=anchor_rows,
                   slice_rows_per_clock_region=height, tile_y_origin=y_origin, tile_y_pitch=y_pitch,
                   reference_point='X: column subslot; Y: lower resource-row anchor; not physical center',
                   resource_geometry=geometry)
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
    p.add_argument('--x-model', choices=['tile-columns', 'rpm'], default='tile-columns')
    p.add_argument('--structure-sites', type=Path)
    p.add_argument('--structure-tiles', type=Path)
    args = p.parse_args()
    print(json.dumps(convert(args.sites, args.output, args.part, args.metadata,
        x_model=args.x_model, structure_sites=args.structure_sites,
        structure_tiles=args.structure_tiles), indent=2))
