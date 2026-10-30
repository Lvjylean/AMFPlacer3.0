"""Read existing AMF outputs into a portable local viewer bundle. Never run Tcl.

This converter runs where the reports live; only the resulting small bundle needs
copying to the Mac. It does not open a DCP or run placement/routing.
"""
import argparse
from collections import Counter, defaultdict
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import zipfile


def resource_type(t):
    for prefix, kind in [('LUT', 'LUT'), ('FD', 'FF'), ('LD', 'FF'), ('DSP', 'DSP'),
                         ('RAMB', 'BRAM'), ('FIFO', 'BRAM'), ('URAM', 'URAM'),
                         ('CARRY', 'CARRY'), ('MUX', 'MUX'), ('RAM', 'LUTRAM/SRL'),
                         ('SRL', 'LUTRAM/SRL')]:
        if t.startswith(prefix): return kind
    return 'OTHER'


def rows(path):
    with Path(path).open() as f:
        yield from csv.DictReader((x for x in f if not x.startswith('#')), delimiter='\t')


def under(name, prefix):
    return name == prefix or name.startswith(prefix + '/')


def membership(name, regions):
    matches = [r for r in regions if under(name, r['prefix'])]
    return max(matches, key=lambda r: len(r['prefix']))['id'] if matches else 'static'


def geometry(cfg):
    sites, cr = {}, defaultdict(list)
    with zipfile.ZipFile(cfg['vivado extracted device information file']) as z:
        with z.open('exportSiteLocation') as f:
            for line in f:
                d = dict(re.findall(r'(\w+)=> (\S+)', line.decode()))
                p = [float(d['centerx']), float(d['centery'])]
                sites[d['site']] = p
                if d['site'].startswith('SLICE'):
                    cr[d['clockRegionName']].append(p)
    phys = Path(cfg['physical boundary model file'])
    boxes_physical, seams = [], []
    with phys.open() as f:
        for line in f:
            parts=line.rstrip().split('\t')
            if parts[0]=='REGION': boxes_physical.append(list(map(float,parts[3:7])))
            elif parts[0]=='BOUNDARY' and parts[2]=='SLR': seams.append(float(parts[4]))
    if not boxes_physical: raise ValueError('Missing physical REGION bounds')
    bounds=[min(b[0] for b in boxes_physical),max(b[1] for b in boxes_physical),
            min(b[2] for b in boxes_physical),max(b[3] for b in boxes_physical)]
    nx, ny = int(cfg['clockRegionXNum']), int(cfg['clockRegionYNum'])
    boxes = {k: [min(p[0] for p in v), max(p[0] for p in v),
                 min(p[1] for p in v), max(p[1] for p in v)] for k, v in cr.items()}
    xs = [bounds[0]] + [(boxes[f'X{x-1}Y0'][1]+boxes[f'X{x}Y0'][0])/2 for x in range(1,nx)] + [bounds[1]]
    ys = [bounds[2]] + [(boxes[f'X0Y{y-1}'][3]+boxes[f'X0Y{y}'][2])/2 for y in range(1,ny)] + [bounds[3]]
    return sites, dict(bounds=bounds, cr_x=xs, cr_y=ys,
                       slr=sorted(set(seams)))


def pblocks(full, meta):
    regions = {r['id']: dict(id=r['id'], name=r['pblock'], prefix=r['partition'],
                             rects=[], ranges=r['derived_ranges']) for r in rows(full/'regions.tsv')}
    for row in rows(full/'resource_regions.tsv'):
        for part in row['clock_regions'].split():
            m = re.fullmatch(r'CLOCKREGION_X(\d+)Y(\d+):CLOCKREGION_X(\d+)Y(\d+)', part)
            if not m: raise ValueError('Unsupported region geometry: '+part)
            x0,y0,x1,y1 = map(int,m.groups())
            if x0>x1 or y0>y1: raise ValueError('Reversed Pblock range')
            regions[row['region']]['rects'].append([meta['cr_x'][x0],meta['cr_x'][x1+1],
                                                   meta['cr_y'][y0],meta['cr_y'][y1+1]])
    if any(not r['rects'] for r in regions.values()): raise ValueError('Missing Pblock geometry')
    return list(regions.values())


def netlist_types(cfg, run):
    native = run/'native/cells.tsv'
    if native.exists(): return {r['cell']: resource_type(r['primitive']) for r in rows(native)}
    if (run/'cells.tsv').exists():
        return {r['cell']: resource_type(r['primitive']) for r in rows(run/'cells.tsv')}
    types = {}
    with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
        member='allCellPinNet' if 'allCellPinNet' in z.namelist() else 'netlist'
        with z.open(member) as f:
            for raw in f:
                if raw.startswith(b'curCell=> '):
                    n,t = raw.decode().strip()[10:].rsplit(' type=> ',1)
                    types[n] = resource_type(t)
    return types


def placed_points(path, sites, types, regions, context=False):
    result = []
    with Path(path).open() as f:
        for line in f:
            fields = line.rstrip('\n').split('\t')
            if fields[0]=='cell': continue
            name, target = fields[:2]
            site = target.split('/')[0]
            if site not in sites: raise ValueError('Missing site coordinate: '+site)
            if name not in types: raise ValueError('Missing cell type: '+name)
            x,y = sites[site]
            result.append([x,y,name,types[name],membership(name,regions),'context' if context else 'active'])
    return result


# This recognises only the quoted data emitted by SllPlanner::exportPlan. No eval,
# source, command substitution, or permissive partial imports of an unknown file.
QUOTED = r'"((?:\\.|[^"\\])*)"'
NET = re.compile(r'^\s*\[dict create net '+QUOTED+r' driver '+QUOTED)
CROSSING = re.compile(r'\[dict create resource '+QUOTED+r' from (\d+) to (\d+) segment \[list')


def unquote(s):
    def repl(m):
        c=m[1]
        if c not in '\\"[]$': raise ValueError('Unsupported Tcl data escape')
        return c
    return re.sub(r'\\(.)',repl,s)


def sll_overlay(plan, channels, regions):
    if plan is None or not Path(plan).exists(): return dict(state='missing',crossings=[],tiles=[])
    inventory, supply, tile_meta = {}, defaultdict(set), {}
    for r in rows(channels):
        a,b,col = int(r['from']),int(r['to']),int(r['column'])
        p,q = [float(r['in_x']),float(r['in_y'])],[float(r['out_x']),float(r['out_y'])]
        key = f'{min(a,b)}:{col}:{min(p[1],q[1]):g}'
        inventory[(r['resource'],a,b)] = dict(tile=key,column=col,entry=p,exit=q)
        supply[key].add(r['resource'])  # Same canonical UBUMP name for both directions.
        tile_meta[key] = dict(id=key,boundary=min(a,b),column=col,
                             low=p if a<b else q, high=q if a<b else p)
    crossings=[];header=False;closed=False
    with Path(plan).open() as f:
        for line in f:
            stripped=line.strip()
            if stripped.startswith('set e2_sll_plan [list'):
                if header:raise ValueError('Duplicate SLL plan header')
                header=True;closed=stripped=='set e2_sll_plan [list]'
            elif stripped==']':closed=True
            match=NET.match(line)
            if not match:
                if line.strip() and not (line.startswith('#') or line.strip() in (']','set e2_sll_plan [list \\', 'set e2_sll_plan [list]')):
                    raise ValueError('Unsupported SLL plan record')
                continue
            if not header or closed:raise ValueError('SLL record outside plan')
            net,driver=map(unquote,match.groups())
            found=CROSSING.findall(line)
            if line.count('[dict create resource ')!=len(found): raise ValueError('Incomplete crossing parse')
            for resource,a,b in found:
                resource=unquote(resource);a,b=int(a),int(b)
                c=inventory[(resource,a,b)]
                crossings.append(dict(c,id=f'{net}:{len(crossings)}',net=net,driver=driver,
                                      resource=resource,from_slr=a,to_slr=b,
                                      region=membership(driver.rsplit('/',1)[0],regions)))
    if not header or not closed:raise ValueError('Incomplete SLL plan file')
    loads=Counter(c['tile'] for c in crossings)
    endpoints=defaultdict(set)
    for c in crossings:
        endpoints[c['tile']].add(tuple(c['entry']))
        endpoints[c['tile']].add(tuple(c['exit']))
    tiles=[dict(tile_meta[k],capacity=len(supply[k]),used=v,points=sorted(endpoints[k])) for k,v in sorted(loads.items())]
    return dict(state='recorded',kind='planned-legalized',crossings=crossings,tiles=tiles,
                capacity_scope='physical canonical channels; planned load, not routed usage',
                source=str(plan))


def write_json(path,data):
    raw=json.dumps(data,ensure_ascii=False,separators=(',',':'),allow_nan=False).encode()
    if path.suffix=='.gz': raw=gzip.compress(raw,compresslevel=6,mtime=0)
    path.write_bytes(raw)


def write_frame(out,name,points,overlay):
    dictionary=dict(names=[],types=[],regions=[],roles=[])
    binary=bytearray()
    seen=set()
    for i,(x,y,n,t,region,role) in enumerate(points):
        if n in seen: raise ValueError('Duplicate cell: '+n)
        if not all(math.isfinite(v) for v in (x,y)): raise ValueError('Non-finite coordinate')
        seen.add(n)
        for k,v in zip(dictionary,(n,t,region,role)): dictionary[k].append(v)
        binary.extend(struct.pack('<Iff',i,x,y))
    (out/(name+'.bin.gz')).write_bytes(gzip.compress(binary,6,mtime=0))
    write_json(out/(name+'.dictionary.json.gz'),dictionary)
    write_json(out/(name+'.layers.json.gz'),overlay)
    return dict(name=name,binary=name+'.bin.gz',dictionary=name+'.dictionary.json.gz',
                layers=name+'.layers.json.gz',cells=len(points),crossings=len(overlay['crossings']),
                counts=dict(Counter(dictionary['types'])))


def build(run,full,placement,out,title):
    run,full,placement,out=map(Path,(run,full,placement,out))
    cfg_path=run/'amf_config.json';cfg=json.loads(cfg_path.read_text())
    sites,meta=geometry(cfg)
    regions=pblocks(full,meta)
    types=netlist_types(cfg,run)
    points=placed_points(placement,sites,types,regions)
    active=cfg.get('Native active module')
    context=run/'native/static_placement.tsv'
    if active:
        if any(p[4]!=active for p in points): raise ValueError('Non-active cell in incremental placement')
        points+=placed_points(context,sites,types,regions,True)
    plan=Path(cfg['dumpDirectory'])/'sll-plan.tcl'
    overlay=sll_overlay(plan,cfg['SllChannelFile'],regions)
    out.mkdir(parents=True,exist_ok=False)
    frame=write_frame(out,'final',points,overlay)
    inputs=[cfg_path,placement,full/'regions.tsv',full/'resource_regions.tsv',plan,Path(cfg['SllChannelFile'])]
    if active: inputs.append(context)
    manifest=dict(schema='amf-local-layout-v1',title=title,run=str(run),device=cfg['device'],
                  part=cfg['physical device part'],mode='incremental' if active else 'full',
                  active_region=active,pblocks=regions,metadata=meta,frames=[frame],
                  coordinate_system='AMF placement coordinates; physical COLUMN only in SLL details',
                  placement_stage='AMF final site/BEL export; before Vivado compatibility placement',
                  history_note='最终布局快照；原实验未保存逐轮 SLL/完整坐标，不能恢复 QP 过程。',
                  static_context='native cached interface context only' if active else None,
                  inputs=[dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in inputs if p.exists()])
    write_json(out/'manifest.json',manifest)
    return dict(output=str(out),cells=frame['cells'],crossings=frame['crossings'],
                tiles=len(overlay['tiles']),bytes=sum(p.stat().st_size for p in out.iterdir()))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('run','full','placement','out','title'): p.add_argument('--'+arg,required=True)
    a=p.parse_args();print(json.dumps(build(**vars(a)),ensure_ascii=False))
