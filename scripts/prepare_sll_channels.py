"""Compile an exported SLL inventory using the existing AMF coordinate map."""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path

def prepare(raw, coordinates, output):
    raw,coordinates,output=map(Path,(raw,coordinates,output))
    if output.exists():raise ValueError('Refuse overwrite: '+str(output))
    mapping=json.loads(coordinates.read_text())
    metadata=dict(x.split('\t',1) for x in (raw/'metadata.tsv').read_text().splitlines())
    if mapping.get('x_kind')!='tile-columns' or mapping['part']!=metadata['part']:
        raise ValueError('SLL/coordinate part or coordinate convention mismatch')
    bases={r['tile_x']:r['base_x'] for r in mapping['tile_columns']}
    pitch=mapping.get('tile_y_pitch',mapping.get('y_pitch',1))
    origin=mapping.get('tile_y_origin',mapping.get('y_origin',0))
    if pitch!=1 or origin!=0:raise ValueError('Unvalidated SLL Y coordinate convention')
    fields=('slow_max','slow_min','fast_max','fast_min')
    models={r['name']:{f:float(r[f]) for f in fields} for r in csv.DictReader((raw/'speed-models.tsv').open(),delimiter='\t')}
    def point(tile):
        m=re.fullmatch(r'.*_X(\d+)Y(\d+)',tile)
        if not m:raise ValueError('Invalid access tile '+tile)
        return bases[int(m[1])],(int(m[2])-origin)/pitch
    count=0
    with output.open('x') as out:
        out.write('#part\t'+mapping['part']+'\n#coordinate_sha256\t'+hashlib.sha256(coordinates.read_bytes()).hexdigest()+'\n')
        out.write('#delay_scope\tIMUX_to_RXD_egress; node_and_pip_speed_models; conservative_transition_envelope; ns\n')
        out.write('resource\tfrom\tto\tcolumn\tin_x\tin_y\tout_x\tout_y\tslow_max\tslow_min\tfast_max\tfast_min\tsegment\n')
        for r in csv.DictReader((raw/'channels-raw.tsv').open(),delimiter='\t'):
            groups=r['node_models'].split('|')+r['pip_models'].split('|')
            values=[sum((max if f.endswith('max') else min)(models[n][f] for n in g.split(',')) for g in groups) for f in fields]
            if not all(0<v<3 for v in values):raise ValueError('Invalid SLL process delay')
            out.write('\t'.join(map(str,[r['resource'],r['from'],r['to'],r['column'],*point(r['in_tile']),*point(r['out_tile']),*values,r['segment']]))+'\n')
            count+=1
    if count!=int(metadata['channels']) or metadata['rejected']!='0':raise ValueError('Incomplete SLL inventory')
    return dict(rows=count,sha256=hashlib.sha256(output.read_bytes()).hexdigest(),coordinate_sha256=hashlib.sha256(coordinates.read_bytes()).hexdigest())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('raw',type=Path);p.add_argument('coordinates',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();print(json.dumps(prepare(a.raw,a.coordinates,a.output),indent=2))
