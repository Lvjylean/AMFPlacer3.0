"""Summarize device-query leaf connectivity without assuming a 12/16 limit.

The result is a nominal connectivity model. Simultaneous routability needs
separate validation; shared leaf sets are one resource domain, not per-SLICE
independent budgets.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import zipfile

from clock_resource_capacity import digest, read_metadata, read_regions, archive_regions


def rows(path):
    with Path(path).open() as f:
        return list(csv.DictReader(f, delimiter='\t'))


def names(value):
    return value.split(',') if value else []


def analyze(raw, fabric, device, out):
    raw, fabric, device, out = map(Path, (raw, fabric, device, out))
    if out.exists():
        raise ValueError('Refusing to overwrite topology output: ' + str(out))
    meta = read_metadata(raw / 'metadata.tsv')
    fabric_meta = read_metadata(fabric / 'metadata.tsv')
    if meta.get('part') != fabric_meta.get('part') or meta.get('vivado') != fabric_meta.get('vivado'):
        raise ValueError('Topology/fabric part or Vivado version differs')
    if meta.get('status') != 'completed' or meta.get('placement_executed') != '0':
        raise ValueError('Expected completed, unplaced topology query')
    regions = read_regions(fabric / 'clock_regions.tsv')
    if archive_regions(device, fabric / 'sites.tsv') != regions:
        raise ValueError('Topology fabric and AMF device archive differ')
    expected = {}
    site_types = {}
    queried_tile_columns = {}
    for r in rows(fabric / 'sites.tsv'):
        m = re.fullmatch(r'SLICE_X([0-9]+)Y([0-9]+)', r['site'])
        if m:
            expected.setdefault((r['clock_region'], int(m[1])), set()).add(int(m[2]))
            site_types[r['site']] = r['site_type']
            tile_match = re.fullmatch(r'.*_X([0-9]+)Y[0-9]+', r.get('tile', ''))
            queried_tile_columns[r['site']] = int(tile_match[1]) if tile_match else None
    # C++ measures every SLICE's half from the smallest site Y in the whole
    # clock region. Per-column 60-row spans alone do not establish that mapping:
    # shifted columns could otherwise appear valid while AMF splits them apart
    # or computes a half index outside [0, 1].
    region_slice_rows = {}
    for (cr, x), ys in expected.items():
        ordered = tuple(sorted(ys))
        if len(ordered) != 60 or ordered != tuple(range(ordered[0], ordered[0] + 60)):
            raise ValueError('AMF SLICE columns require the same contiguous 60-row range per CR')
        if cr in region_slice_rows and region_slice_rows[cr] != ordered:
            raise ValueError('AMF SLICE columns require the same contiguous 60-row range per CR')
        region_slice_rows[cr] = ordered
    region_bottom_site_y = {cr: ys[0] for cr, ys in region_slice_rows.items()}
    # AMF forms its occupied half columns using tile X from the actual ZIP,
    # not SLICE X. Check that exact grouping, including the existing Y divider.
    site_tile_columns, region_tile_y = {}, {}
    with zipfile.ZipFile(device) as archive:
        member = next(n for n in archive.namelist() if not n.endswith('/'))
        with archive.open(member) as f:
            for line in f:
                text = line.decode('utf-8')
                m = re.search(r'^site=>\s+(SLICE_X[0-9]+Y[0-9]+)\s+tile=>\s+\S+_X([0-9]+)Y([0-9]+).*?clockRegionName=>\s+(X[0-9]+Y[0-9]+)', text)
                if not m:
                    continue
                site, tile_x, tile_y, cr = m.groups()
                tile_x = int(tile_x)
                if queried_tile_columns.get(site) not in (None, tile_x):
                    raise ValueError('AMF archive tile column differs from queried fabric: ' + site)
                site_tile_columns[site] = tile_x
                region_tile_y.setdefault(cr, set()).add(int(tile_y))
    covered = {}
    sources = {}
    for r in rows(raw / 'leaf_sources.tsv'):
        leaf = r['leaf_node']
        hdistr = names(r['hdistr_nodes'])
        if leaf in sources or len(set(hdistr)) != len(hdistr) or len(hdistr) != int(r['hdistr_count']) or not hdistr:
            raise ValueError('Invalid leaf source: ' + leaf)
        sources[leaf] = r
    pin_evidence = {}
    for r in rows(raw / 'pin_leaf_reachability.tsv'):
        if r['pin'] in pin_evidence:
            raise ValueError('Duplicate pin evidence')
        pin_evidence[r['pin']] = r
    consumed_pins = set()
    domains, leaf_owner, all_halves = {}, {}, []
    amf_groups = {}
    known_amf_geometry = (set(site_tile_columns) == set(site_types) and set(region_tile_y) == set(regions)
                          and all((max(ys) - min(ys) + 1) // 2 == 30 for ys in region_tile_y.values()))
    for r in rows(raw / 'half_columns.tsv'):
        cr, x, low, high = r['clock_region'], int(r['slice_x']), int(r['y_min']), int(r['y_max'])
        if cr not in regions or high - low != 29:
            raise ValueError('Unknown region or unsupported half-column geometry')
        bottom = region_bottom_site_y.get(cr)
        if bottom is None or (low, high) not in ((bottom, bottom + 29), (bottom + 30, bottom + 59)):
            raise ValueError('Half-column range differs from the AMF CR-relative Y partition')
        key = (cr, x)
        ys = set(range(low, high + 1))
        seen = covered.setdefault(key, set())
        if seen & ys:
            raise ValueError('Overlapping or duplicate half-column inventory')
        seen.update(ys)
        leaves = tuple(sorted(names(r['leaf_nodes'])))
        if len(set(leaves)) != len(leaves) or len(leaves) != int(r['leaf_count']) or not leaves:
            raise ValueError('Invalid leaf count')
        if any(leaf not in sources for leaf in leaves):
            raise ValueError('Missing leaf source details')
        for leaf in leaves:
            if leaf in leaf_owner and leaf_owner[leaf] != leaves:
                raise ValueError('Partially overlapping leaf sets need a richer sharing model')
            leaf_owner[leaf] = leaves
        pins = names(r['checked_pins'])
        expected_pins = set()
        for y in (low, high):
            site = f'SLICE_X{x}Y{y}'
            kind = site_types.get(site)
            if kind not in ('SLICEL', 'SLICEM'):
                raise ValueError('Unknown endpoint site')
            for suffix in ['CLK1', 'CLK2'] + (['LCLK'] if kind == 'SLICEM' else []):
                expected_pins.add(site + '/' + suffix)
        if set(pins) != expected_pins or len(pins) != len(expected_pins):
            raise ValueError('Missing or duplicate endpoint clock-pin checks')
        for pin in pins:
            evidence = pin_evidence.get(pin)
            site = pin.rsplit('/', 1)[0]
            if (not evidence or evidence['site'] != site or evidence['site_type'] != site_types[site]
                    or evidence['clock_region'] != cr or not evidence['start_nodes']
                    or int(evidence['leaf_count']) != len(leaves)
                    or tuple(sorted(names(evidence['leaf_nodes']))) != leaves):
                raise ValueError('Endpoint pin evidence disagrees with half-column: ' + pin)
            consumed_pins.add(pin)
        member = dict(slice_x=x, y_min=low, y_max=high, site_types=names(r['site_types']), checked_pins=pins)
        tile_columns = {site_tile_columns.get(f'SLICE_X{x}Y{y}') for y in range(low, high + 1)}
        if len(tile_columns) != 1 or None in tile_columns:
            known_amf_geometry = False
        else:
            tile_x = next(iter(tile_columns))
            member['tile_x'] = tile_x
            amf_groups.setdefault((cr, tile_x, low, high), set()).add((x, low, high))
        domain = domains.setdefault(leaves, dict(clock_region=cr, slr=regions[cr], nominal_leaf_capacity=len(leaves),
                                               leaf_nodes=list(leaves), slice_half_columns=[]))
        if domain['clock_region'] != cr:
            raise ValueError('Leaf domain unexpectedly crosses a clock region')
        if domain['slice_half_columns'] and any((m['y_min'], m['y_max']) != (low, high)
                                                for m in domain['slice_half_columns']):
            raise ValueError('Shared leaf domain has inconsistent vertical extent')
        domain['slice_half_columns'].append(member)
        all_halves.append(r)
    if covered != expected:
        raise ValueError('Topology query does not cover every fabric SLICE site exactly')
    if set(leaf_owner) != set(sources):
        raise ValueError('Unreferenced or missing leaf source')
    if consumed_pins != set(pin_evidence):
        raise ValueError('Unreferenced endpoint pin evidence')
    result = list(domains.values())
    result.sort(key=lambda d: (d['slr'], d['clock_region'], d['slice_half_columns'][0]['slice_x'],
                               d['slice_half_columns'][0]['y_min']))
    for i, domain in enumerate(result):
        domain['id'] = i
    topology_partition = {frozenset((d['clock_region'], m['slice_x'], m['y_min'], m['y_max'])
                                    for m in d['slice_half_columns']) for d in result}
    amf_partition = {frozenset((key[0], *m) for m in members) for key, members in amf_groups.items()}
    per_region = {}
    for cr in sorted(regions):
        ds = [d for d in result if d['clock_region'] == cr]
        per_region[cr] = dict(slr=regions[cr], domains=len(ds),
                              nominal_leaf_capacities=sorted({d['nominal_leaf_capacity'] for d in ds}))
    summary = dict(schema='amf-clock-half-column-connectivity-v1', part=meta['part'], vivado_version=meta['vivado'],
                   resource_state='nominal', capacity_source='vivado-device-node-connectivity',
                   scope='SLICE CLK1/CLK2 and SLICEM LCLK; endpoint checks of each 30-row half',
                   device_sha256=digest(device), clock_regions=len(regions), slice_half_columns=len(all_halves),
                   shared_leaf_domains=len(result), unique_leaf_nodes=len(sources),
                   nominal_leaf_capacities=sorted({d['nominal_leaf_capacity'] for d in result}),
                   hdistr_sources_per_leaf=sorted({int(r['hdistr_count']) for r in sources.values()}),
                   member_columns_per_domain=sorted({len(d['slice_half_columns']) for d in result}),
                   amf_slice_half_column_partition_matches=known_amf_geometry and topology_partition == amf_partition,
                   clock_routability_verified=False, geometry_assumptions=meta.get('geometry_assumptions'),
                   metadata=meta, regions=per_region,
                   sources={str(p.resolve()): digest(p) for p in sorted(raw.iterdir()) if p.is_file()})
    out.mkdir(parents=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out / 'half_column_domains.json').write_text(json.dumps(dict(summary=summary, domains=result), indent=2) + '\n')
    with (out / 'half_column_capacities.tsv').open('w') as f:
        writer = csv.writer(f, delimiter='\t')
        writer.writerow(['domain', 'clock_region', 'slr', 'nominal_leaf_capacity', 'member_slice_x', 'y_min', 'y_max'])
        for d in result:
            members = d['slice_half_columns']
            writer.writerow([d['id'], d['clock_region'], d['slr'], d['nominal_leaf_capacity'],
                             ','.join(str(m['slice_x']) for m in members), members[0]['y_min'], members[0]['y_max']])
    return summary


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw-dir', required=True)
    p.add_argument('--fabric-raw-dir', required=True)
    p.add_argument('--device', required=True)
    p.add_argument('--output', required=True)
    a = p.parse_args()
    summary = analyze(a.raw_dir, a.fabric_raw_dir, a.device, a.output)
    print(json.dumps({k: v for k, v in summary.items() if k not in ('regions', 'sources', 'metadata')}, indent=2))
