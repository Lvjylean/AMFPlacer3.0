"""Tile-column metric: unit column bases, +/- 1/4 subslots, explicit IP columns.

RPM is used to recover physical order and interpolate non-fabric locations,
not as a globally scaled distance. Every extra column has provenance in the
full-device site/tile export. Column width is a placement convention, not um.
"""
from bisect import bisect_right
from collections import defaultdict
import csv
import math
import re

SPECIAL = re.compile(r'^(HPIOB_(M|S|SNGL)|CONFIG_SITE|PCIE[0-9].*|CMAC.*)$')


def validate_x_map(mapping):
    if mapping.get('x_kind') == 'tile-columns':
        pairs = mapping['rpm_x_anchors']
        if len(pairs) < 2 or any(not math.isfinite(v) for p in pairs for v in p):
            raise ValueError('Invalid X anchors')
        if any(b[0] <= a[0] or b[1] <= a[1] for a,b in zip(pairs,pairs[1:])):
            raise ValueError('Non-monotone X anchors')
    elif mapping.get('x_kind', 'rpm-affine') != 'rpm-affine' or mapping.get('rpm_x_pitch',0) <= 0:
        raise ValueError('Unknown/invalid X mapping')


def mapped_x(value, mapping):
    if mapping.get('x_kind') != 'tile-columns':
        return (value-mapping['rpm_x_origin'])/mapping['rpm_x_pitch']
    pairs = mapping['rpm_x_anchors']
    # Maps are small (hundreds of columns); no mutable global cache.
    i = max(0,min(len(pairs)-2,bisect_right(pairs,[value,float('inf')])-1))
    a,b = pairs[i:i+2]
    return a[1]+(value-a[0])*(b[1]-a[1])/(b[0]-a[0])


def derive_x_map(rows, structure_sites, structure_tiles):
    by_column = defaultdict(dict)
    for row in rows:
        tx = int(re.search(r'_X(\d+)Y\d+$',row['tile'])[1])
        by_column[tx].setdefault(row['rpm_x'],set()).add(row['tile_type'])
    columns = sorted(by_column)
    if columns != list(range(columns[0],columns[-1]+1)):
        raise ValueError('Non-contiguous fabric tile X skeleton')
    if any(len(by_column[x]) not in (1,2) for x in columns):
        raise ValueError('Expected one or two fabric subcolumns per tile X')
    if any(max(by_column[a]) >= min(by_column[b]) for a,b in zip(columns,columns[1:])):
        raise ValueError('Fabric tile X order disagrees with RPM order')
    type_offsets = {}
    for slots in by_column.values():
        if len(slots) != 2: continue
        for rpm,offset in zip(sorted(slots),(-.25,.25)):
            for typ in slots[rpm]:
                if type_offsets.setdefault(typ,offset) != offset:
                    raise ValueError('Tile type has inconsistent left/right placement: '+typ)
    for slots in by_column.values():
        for types in slots.values():
            if any(t not in type_offsets for t in types):
                raise ValueError('Cannot infer singleton fabric side from paired columns')
            if len({type_offsets[t] for t in types}) != 1:
                raise ValueError('Shared RPM position has conflicting subslots')
    special_rows = []
    raw_fabric = {}
    names = {r['site'] for r in rows}
    with open(structure_sites) as f:
        for r in csv.DictReader(f,delimiter='\t'):
            if r['site'] in names:
                if r['site'] in raw_fabric: raise ValueError('Duplicate structure site')
                raw_fabric[r['site']] = (r['tile'],float(r['rpm_x']),int(r['slr']))
            if SPECIAL.fullmatch(r['site_type']): special_rows.append(r)
    for r in rows:
        if raw_fabric.get(r['site']) != (r['tile'],r['rpm_x'],r['slr']):
            raise ValueError('Fabric/full-structure source mismatch: '+r['site'])
    wanted = {r['tile'] for r in special_rows}
    tiles = {}
    with open(structure_tiles) as f:
        for r in csv.DictReader(f,delimiter='\t'):
            if r['tile'] in wanted: tiles[r['tile']] = r
    groups = defaultdict(list)
    for s in special_rows:
        if s['tile'] not in tiles: raise ValueError('Missing special resource tile')
        groups[int(tiles[s['tile']]['column'])].append(s)
    extra = []
    maxima = [max(by_column[x]) for x in columns]
    for physical_column,items in sorted(groups.items()):
        rx = {float(s['rpm_x']) for s in items}
        if len(rx) != 1: raise ValueError('Ambiguous special column RPM position')
        rx = rx.pop()
        i = bisect_right(maxima,rx)
        if i == 0 or i == len(columns):
            raise ValueError('External special columns need an explicit geometry rule')
        left,right = columns[i-1],columns[i]
        if not max(by_column[left]) < rx < min(by_column[right]):
            raise ValueError('Special resource overlaps fabric tile subslots')
        tile_types = sorted({tiles[s['tile']]['tile_type'] for s in items})
        # Explicit side marker for half-column I/O. Whole hard blocks use center.
        offsets = {-.25 if t.endswith('_L') else .25 if t.endswith('_R') else 0.0 for t in tile_types}
        if len(offsets) != 1: raise ValueError('Ambiguous special column subslot')
        extra.append(dict(physical_column=physical_column,rpm_x=rx,after_tile_x=left,
            before_tile_x=right,width=1.0,offset=offsets.pop(),tile_types=tile_types,
            site_types=sorted({s['site_type'] for s in items}),site_count=len(items),
            slrs=sorted({int(s['slr']) for s in items}),
            clock_regions=sorted({s['clock_region'] for s in items})))
    if any(a['rpm_x']>=b['rpm_x'] for a,b in zip(extra,extra[1:])):
        raise ValueError('Physical special-column order disagrees with RPM')
    bases = {x:float(x+sum(e['after_tile_x']<x for e in extra)) for x in columns}
    anchors = {}
    audit = []
    for x in columns:
        positions=[]
        for rpm,types in sorted(by_column[x].items()):
            offset=type_offsets[next(iter(types))];v=bases[x]+offset
            if rpm in anchors and anchors[rpm]!=v:raise ValueError('Ambiguous X anchor')
            anchors[rpm]=v;positions.append(dict(rpm_x=rpm,offset=offset,x=v,tile_types=sorted(types)))
        audit.append(dict(tile_x=x,base_x=bases[x],positions=positions))
    for index,e in enumerate(extra):
        e['base_x']=float(e['after_tile_x']+1+index)
        e['x']=e['base_x']+e['offset']
        anchors[e['rpm_x']]=e['x']
    result=dict(x_kind='tile-columns',rpm_x_anchors=[list(p) for p in sorted(anchors.items())],
        tile_columns=audit,special_columns=extra,type_offsets=type_offsets,
        x_rule='X = tile_X + count(unrepresented special physical columns to left) + subslot',
        special_width_policy='one metric column per unrepresented special site COLUMN; not physical width or a routing blockage',
        nonfabric_x_policy='piecewise-linear RPM interpolation through fabric/special anchors; linear end extrapolation')
    validate_x_map(result)
    return result
