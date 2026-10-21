"""Generate display-only resource inventory from Vivado exports; no inferred hard-IP sizes."""
from pathlib import Path
import csv, collections, hashlib, json, re


PALETTE = {'PS': '#bd9bff', 'NoC': '#59d5d1', 'CPM5': '#f3b873', 'GTYP': '#f482a7'}

def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter='\t'))

def family(kind):
    if kind == 'PS9': return 'PS'
    if kind.startswith(('NOC_', 'NPI_')): return 'NoC'
    if kind == 'CPM5': return 'CPM5'
    if kind.startswith('GTYP_'): return 'GTYP'
    raise ValueError('Unclassified actual site type: '+kind)

def build(geometry, raw, geometry_path):
    regions = []
    for name in ['upper', 'lower']:
        tiles = {r['tile']: r for r in rows(raw/f'{name}-tiles.tsv')}
        sites = rows(raw/f'{name}-sites.tsv')
        row_ids = {int(re.fullmatch(r'X\d+Y(\d+)', s['cr'])[1]) for s in sites}
        bands = []
        for y in sorted(row_ids):
            fabric = [r for r in geometry['clock_regions'] if r['slice_count'] > 0
                      and int(re.fullmatch(r'X\d+Y(\d+)', r['name'])[1]) == y]
            assert fabric
            # Left complement of the displayed SLICE-containing CR envelopes.
            bands.append([geometry['bounds'][0], min(r['bounds'][0] for r in fabric),
                          min(r['bounds'][2] for r in fabric), max(r['bounds'][3] for r in fabric)])
        groups = collections.defaultdict(list)
        for s in sites:
            tile = tiles[s['tile']]
            point = [int(tile['column']), geometry['tile_rows_reversed_at']-int(tile['row'])]
            assert any(l <= point[0] <= h and b <= point[1] <= t for l,h,b,t in bands)
            groups[family(s['type'])].append(dict(s, position=point, tile_type=tile['type']))
        resources = []
        for kind, ss in groups.items():
            anchors = {}
            for s in ss:
                a = anchors.setdefault(s['tile'], dict(tile=s['tile'], position=s['position'], sites=[]))
                a['sites'].append(s['site'])
            resources.append(dict(kind=kind, color=PALETTE[kind], sites=ss,
                                  site_types=dict(collections.Counter(s['type'] for s in ss)),
                                  anchors=list(anchors.values())))
        resources.sort(key=lambda r: -sum(a['position'][1] for a in r['anchors'])/len(r['anchors']))
        regions.append(dict(id=name, title='上方专用资源区' if name=='upper' else '下方专用资源区',
                            slr=next(r['slr'] for r in geometry['clock_regions'] if r['name']==sites[0]['cr']),
                            clock_regions=sorted({s['cr'] for s in sites}), bands=bands,
                            label_bounds=[max(b[0] for b in bands),min(b[1] for b in bands),
                                          min(b[2] for b in bands),max(b[3] for b in bands)],
                            slice_count=0, resources=resources))
    return dict(schema=1, part=geometry['part'], device_input_sha256=geometry['device_input_sha256'],
                geometry_sha256=hashlib.sha256(geometry_path.read_bytes()).hexdigest(),
                coordinate_system=geometry['coordinate_system'], palette=PALETTE,
                coverage='Two queried left-hand non-SLICE areas only; not whole-device hard-IP inventory.',
                semantics='Dots are actual tile anchors of sites; colored labels are annotations, not hard-IP footprints. NoC edges are not inferred.',
                source=dict(format='Vivado tile/site TSV exports',
                            sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in raw.glob('*.tsv')}),
                regions=regions)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--geometry', type=Path, required=True)
    parser.add_argument('--exports', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = build(json.loads(args.geometry.read_text()), args.exports, args.geometry)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print('Saved', args.output, 'sites', sum(len(r['sites']) for a in result['regions'] for r in a['resources']))
