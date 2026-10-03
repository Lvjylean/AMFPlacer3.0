#!/usr/bin/env python3
"""Generate an auditable boundary model without changing AMF fabric coordinates."""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import csv
import hashlib
import html
import json
import math
from pathlib import Path
import re
import zipfile
from fabric_x_coordinates import mapped_x, validate_x_map

RESOURCE_KEYS = ('LUT','FF','MLUT','CARRY','MUX7','MUX8','MUX9','DSP','BRAM18','BRAM36','URAM')
FABRIC_RE = re.compile(r'site=> (\S+) tile=> (\S+) clockRegionName=> (\S+) sitetype=> (\S+) tiletype=> (\S+) centerx=> (\S+) centery=> (\S+).* slr=> (\d+) prohibited=> ([01])$')

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

def table(path):
    with Path(path).open() as f:
        yield from csv.DictReader(f,delimiter='\t')

def read_fabric(path):
    rows=[]
    with zipfile.ZipFile(path) as z, z.open('exportSiteLocation') as f:
        for raw in f:
            m=FABRIC_RE.fullmatch(raw.decode().strip())
            if not m: raise ValueError('Physical model requires fabric-device-v2')
            name,tile,cr,typ,tile_type,x,y,slr,prohibited=m.groups()
            rows.append(dict(site=name,tile=tile,clock_region=cr,site_type=typ,
                             x=float(x),y=float(y),slr=int(slr),prohibited=int(prohibited)))
    if not rows or len({r['site'] for r in rows})!=len(rows):
        raise ValueError('Empty or duplicate fabric')
    return rows

def mapped_y(value,mapping):
    pairs=mapping['rpm_y_anchors']
    xs=[p[0] for p in pairs]
    i=max(0,min(len(pairs)-2,bisect_right(xs,value)-1))
    a,b=pairs[i:i+2]
    return a[1]+(value-a[0])*(b[1]-a[1])/(b[0]-a[0])

def make_geometry(fabric,sites,tiles,mapping,rules,part):
    if not re.fullmatch(rules['part_pattern'],part,re.I):
        raise ValueError('Unsupported architecture: '+part)
    anchors=mapping['rpm_y_anchors']
    if len(anchors)<2 or any(b[0]<=a[0] or b[1]<=a[1] for a,b in zip(anchors,anchors[1:])):
        raise ValueError('Non-monotone coordinate anchors')
    validate_x_map(mapping)
    if mapping['part']!=part:
        raise ValueError('Coordinate map mismatch')
    raw={s['site']:s for s in sites}
    if len(raw)!=len(sites): raise ValueError('Duplicate raw physical site')
    tile_by_name={t['tile']:t for t in tiles}
    slice_rows=[r for r in fabric if r['site_type'] in ('SLICEL','SLICEM')]
    if not slice_rows: raise ValueError('No slice geometry')
    # Check every existing fabric site's X and every slice Y against the shared map.
    for row in fabric:
        s=raw.get(row['site'])
        if not s: raise ValueError('Missing raw fabric site: '+row['site'])
        x=mapped_x(float(s['rpm_x']),mapping)
        if abs(x-row['x'])>1e-5 or int(s['slr'])!=row['slr']:
            raise ValueError('Fabric coordinate/SLR mismatch: '+row['site'])
        if row['site_type'] in ('SLICEL','SLICEM') and abs(mapped_y(float(s['rpm_y']),mapping)-row['y'])>1e-5:
            raise ValueError('Fabric Y mismatch: '+row['site'])
    by_slr=defaultdict(list)
    for r in slice_rows: by_slr[r['slr']].append(r)
    ordered=sorted(by_slr,key=lambda sid:min(r['y'] for r in by_slr[sid]))
    ranges={sid:(min(r['y'] for r in by_slr[sid]),max(r['y'] for r in by_slr[sid])) for sid in ordered}
    if any(ranges[a][1]>=ranges[b][0] for a,b in zip(ordered,ordered[1:])):
        raise ValueError('Only horizontally aligned, vertically stacked SLRs are supported')
    xmin=min(r['x'] for r in fabric)-0.5
    xmax=max(r['x'] for r in fabric)+0.5
    ymin=min(r['y'] for r in slice_rows)-0.5
    ymax=max(r['y'] for r in slice_rows)+0.5
    seams=[(ranges[a][1]+ranges[b][0])/2 for a,b in zip(ordered,ordered[1:])]
    boundaries=[]
    for i,y in enumerate(seams):
        boundaries.append(dict(id='slr-seam-'+str(i),kind='SLR',orientation='Y',coordinate=y,
            low=xmin,high=xmax,penalty_ns=rules['slr_delay_ns'],active=True,
            source='adjacent-fabric-slr-rows',confidence='structural',slrs=[ordered[i],ordered[i+1]]))
    io_groups=defaultdict(list)
    candidates=[]
    unknown=Counter()
    support=Counter()
    for s in sites:
        typ=s['site_type']
        if typ in rules['resource_slots']: continue
        if re.fullmatch(rules['io_site_pattern'],typ):
            io_groups[float(s['rpm_x'])].append(s)
        elif re.fullmatch(rules['local_ip_site_pattern'],typ):
            t=tile_by_name.get(s['tile'])
            candidates.append(dict(site=s['site'],kind='LOCAL_IP',site_type=typ,slr=int(s['slr']),
                tile=s['tile'],tile_type=t['tile_type'] if t else None,
                raw_rpm=[float(s['rpm_x']),float(s['rpm_y'])],
                amf=[mapped_x(float(s['rpm_x']),mapping),
                     mapped_y(float(s['rpm_y']),mapping)],
                configured_penalty_ns=rules['local_ip_delay_ns'],active=False,
                reason='local footprint does not establish an unavoidable crossing'))
        elif re.fullmatch(rules['known_support_pattern'],typ): support[typ]+=1
        else: unknown[typ]+=1
    # A band is charged once, even across several SLRs. Require architecture-
    # recognized tiles, complete CR-row coverage and fabric on both sides in each SLR.
    active_x=[]
    for io_id,(rpm_x,group) in enumerate(sorted(io_groups.items())):
        x=mapped_x(rpm_x,mapping)
        failures=[]
        expected_rows={int(re.fullmatch(r'X\d+Y(\d+)',r['clock_region'])[1]) for r in slice_rows}
        observed_rows=set()
        for s in group:
            cr=re.fullmatch(r'X\d+Y(\d+)',s['clock_region'])
            if cr: observed_rows.add(int(cr[1]))
            t=tile_by_name.get(s['tile'])
            if not t or not re.match(rules['io_tile_pattern'],t['tile_type']):
                failures.append('unrecognized-or-missing-io-tile')
        if expected_rows-observed_rows: failures.append('incomplete-clock-region-row-coverage')
        for sid in ordered:
            if not any(r['x']<x for r in by_slr[sid]) or not any(r['x']>x for r in by_slr[sid]):
                failures.append('not-an-internal-band')
        active=not failures
        boundaries.append(dict(id='io-band-'+str(io_id),kind='IO',orientation='X',
            coordinate=x,low=ymin,high=ymax,penalty_ns=rules['io_delay_ns'],active=active,
            source='known-io-sites-and-tiles-full-row-coverage',confidence='architectural' if active else 'candidate',
            site_count=len(group),tile_count=len({s['tile'] for s in group}),raw_rpm_x=rpm_x,
            tile_columns=sorted({int(tile_by_name[s['tile']]['column']) for s in group if s['tile'] in tile_by_name}),
            clock_region_columns=sorted({int(re.fullmatch(r'X(\d+)Y\d+',s['clock_region'])[1]) for s in group if re.fullmatch(r'X(\d+)Y\d+',s['clock_region'])}),
            reasons=sorted(set(failures))))
        if active: active_x.append(x)
    xs=[xmin]+active_x+[xmax]
    ys=[ymin]+seams+[ymax]
    regions=[]
    for iy,sid in enumerate(ordered):
        for ix in range(len(xs)-1):
            regions.append(dict(id=len(regions),slr=sid,x0=xs[ix],x1=xs[ix+1],y0=ys[iy],y1=ys[iy+1],
                                capacity={k:0 for k in RESOURCE_KEYS},site_count=0,prohibited_count=0))
    for row in fabric:
        iy=ordered.index(row['slr'])
        ix=bisect_right(active_x,row['x'])
        reg=regions[iy*(len(xs)-1)+ix]
        if not(reg['y0']<=row['y']<reg['y1']):
            raise ValueError('Resource extent is outside inferred SLR: '+row['site'])
        row['region']=reg['id']
        reg['site_count']+=1
        if row['prohibited']:
            reg['prohibited_count']+=1
            continue
        if row['site_type'] not in rules['resource_slots']:
            raise ValueError('Unknown fabric capacity: '+row['site_type'])
        for k,v in rules['resource_slots'][row['site_type']].items(): reg['capacity'][k]+=v
    if any(not r['capacity']['LUT'] for r in regions):
        raise ValueError('Empty fabric region; explicit irregular topology support required')
    return dict(schema='amf-physical-boundaries-v1',part=part,
                coordinate_bounds=[xmin,xmax,ymin,ymax],regions=regions,boundaries=boundaries,
                local_ip_candidates=candidates,unknown_site_types=dict(unknown),
                known_support_sites=dict(support),slr_order=ordered,
                capacity_contract='nested LUT/MLUT; BRAM18+2*BRAM36 demand shares BRAM18 capacity')

def write_svg(report,path):
    xmin,xmax,ymin,ymax=report['coordinate_bounds']
    sx=620/(xmax-xmin); sy=800/(ymax-ymin)
    def x(v): return 70+(v-xmin)*sx
    def y(v): return 50+(ymax-v)*sy
    colors=['#e3edf8','#e7f3ea']
    out=['<svg xmlns="http://www.w3.org/2000/svg" width="880" height="930" viewBox="0 0 880 930">',
         '<rect width="880" height="930" fill="white"/>',
         '<text x="70" y="26" font-family="sans-serif" font-size="18">AMF physical regions: '+html.escape(report['part'])+'</text>']
    for r in report['regions']:
        out.append(f'<rect x="{x(r["x0"]):.2f}" y="{y(r["y1"]):.2f}" width="{(r["x1"]-r["x0"])*sx:.2f}" height="{(r["y1"]-r["y0"])*sy:.2f}" fill="{colors[r["id"]%2]}" stroke="#999"/>')
        out.append(f'<text x="{x((r["x0"]+r["x1"])/2):.1f}" y="{y((r["y0"]+r["y1"])/2):.1f}" font-family="sans-serif" font-size="15" text-anchor="middle">SLR{r["slr"]} / R{r["id"]}</text>')
    for b in report['boundaries']:
        if not b['active']: continue
        if b['orientation']=='Y': a,c,d,e=x(b['low']),y(b['coordinate']),x(b['high']),y(b['coordinate'])
        else: a,c,d,e=x(b['coordinate']),y(b['low']),x(b['coordinate']),y(b['high'])
        color='#b32132' if b['kind']=='SLR' else '#a56800'
        out.append(f'<line x1="{a}" y1="{c}" x2="{d}" y2="{e}" stroke="{color}" stroke-width="4"/>')
    out+=['<text x="70" y="883" font-family="sans-serif" font-size="14">Red: SLR 1.5 ns/seam; amber: I/O band 0.5 ns/crossing (heuristics).</text>',
          '<text x="70" y="908" font-family="sans-serif" font-size="14">Local IP footprints are reported separately; no unproven hard blockage.</text>','</svg>']
    path.write_text('\n'.join(out)+'\n')

def build(raw_dir,fabric_path,coordinates,rules_path,out):
    raw_dir,fabric_path,coordinates,rules_path,out=map(Path,(raw_dir,fabric_path,coordinates,rules_path,out))
    if out.exists(): raise ValueError('Refusing to overwrite model directory: '+str(out))
    metadata=dict(line.split('\t',1) for line in (raw_dir/'structure_metadata.tsv').read_text().splitlines())
    if metadata.get('scope')!='full-device-sites-and-tiles': raise ValueError('Wrong export scope')
    rules=json.loads(rules_path.read_text()); mapping=json.loads(coordinates.read_text())
    fabric=read_fabric(fabric_path)
    report=make_geometry(fabric,list(table(raw_dir/'structure_sites.tsv')),list(table(raw_dir/'structure_tiles.tsv')),mapping,rules,metadata['part'])
    # Include complete input provenance. Raw physical files remain server-side.
    sources={name:dict(path=str(path.resolve()),sha256=digest(path)) for name,path in {
        'fabric':fabric_path,'coordinates':coordinates,'rules':rules_path,
        **{name:raw_dir/name for name in ('structure_sites.tsv','structure_tiles.tsv','structure_metadata.tsv','clock_regions.tsv','iobanks.tsv')}
        }.items()}
    report['sources']=sources
    report['generator_sha256']=digest(Path(__file__))
    report['x_helper_sha256']=digest(Path(__file__).with_name('fabric_x_coordinates.py'))
    report['export_metadata']=metadata
    report['rules_version']=rules['version']
    out.mkdir(parents=True)
    model=out/'physical_structure.tsv'
    with model.open('w') as f:
        f.write('AMF_PHYSICAL_STRUCTURE\t1\n')
        for k,v in {'part':metadata['part'],'rules_version':rules['version'],'fabric_sha256':sources['fabric']['sha256'],
                    'coordinates_sha256':sources['coordinates']['sha256'],'rules_sha256':sources['rules']['sha256'],
                    'raw_sites_sha256':sources['structure_sites.tsv']['sha256'],
                    'raw_tiles_sha256':sources['structure_tiles.tsv']['sha256'],'site_count':len(fabric)}.items():
            f.write(f'META\t{k}\t{v}\n')
        for r in report['regions']:
            f.write('\t'.join(map(str,['REGION',r['id'],r['slr'],r['x0'],r['x1'],r['y0'],r['y1']]+[r['capacity'][k] for k in RESOURCE_KEYS]))+'\n')
        for b in report['boundaries']:
            f.write('\t'.join(map(str,['BOUNDARY',b['id'],b['kind'],b['orientation'],b['coordinate'],b['low'],b['high'],b['penalty_ns'],int(b['active'])]))+'\n')
        for s in sorted(fabric,key=lambda r:r['site']):
            f.write('\t'.join(map(str,['SITE',s['site'],s['site_type'],s['x'],s['y'],s['slr'],s['prohibited'],s['region']]))+'\n')
    report['model_sha256']=digest(model)
    (out/'boundaries.json').write_text(json.dumps(report,indent=2)+'\n')
    write_svg(report,out/'physical_regions.svg')
    return report

def validate_model_inputs(model_path,fabric_path):
    model_path=Path(model_path)
    report=json.loads(model_path.with_name('boundaries.json').read_text())
    if digest(model_path)!=report['model_sha256']: raise ValueError('Physical model hash mismatch')
    if digest(fabric_path)!=report['sources']['fabric']['sha256']: raise ValueError('Physical model/fabric mismatch')
    for name,item in report['sources'].items():
        if digest(item['path'])!=item['sha256']: raise ValueError('Physical model provenance changed: '+name)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('raw-dir','fabric','coordinates','rules','out'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    result=build(a.raw_dir,a.fabric,a.coordinates,a.rules,a.out)
    print(json.dumps({k:result[k] for k in ('part','regions','boundaries','unknown_site_types','model_sha256')},indent=2))
