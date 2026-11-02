"""Validate complete FF polarity coverage and bind fresh metadata to frozen inputs."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for data in iter(lambda:f.read(4*1024*1024),b''):h.update(data)
    return h.hexdigest()

def rows(path):
    with Path(path).open() as f:
        return list(csv.DictReader(f,delimiter='\t'))

def validate(path, cells_path):
    expected={row['cell_name']:row['primitive'] for row in rows(cells_path)
              if row['primitive'] in {'FDRE','FDSE','FDCE','FDPE','LDCE','AND2B1L'}}
    seen=set();polarities=Counter()
    with Path(path).open() as f:
        if f.readline().rstrip('\n')!='AMF_FF_CONTROL_POLARITIES 1':raise ValueError('Invalid FF metadata header')
        for line in f:
            fields=line.split()
            if len(fields)!=5:raise ValueError('Invalid FF metadata record')
            cell,kind,clock,sr,ce=fields
            if cell in seen or expected.get(cell)!=kind or clock not in ('0','1') or sr not in ('0','1') or ce!='0':
                raise ValueError('Mismatched, duplicate or unsupported FF metadata: '+cell)
            seen.add(cell);polarities[(clock,sr,ce)]+=1
    if seen!=set(expected):raise ValueError('Incomplete FF metadata coverage')
    return {'cells':len(seen),'polarities':{','.join(k):v for k,v in sorted(polarities.items())},'sha256':digest(path)}

def materialize(directory, cells_path, target):
    expected={row['cell_name']:row['primitive'] for row in rows(cells_path)
              if row['primitive'] in {'FDRE','FDSE','FDCE','FDPE','LDCE','AND2B1L'}}
    counts={row['primitive']:int(row['count']) for row in rows(directory/'ff_control_counts.tsv')}
    if set(counts)!={'FDRE','FDSE','FDCE','FDPE','LDCE','AND2B1L'} or any(Counter(expected.values())[k]!=v for k,v in counts.items()):
        raise ValueError('FF inventory and complete inversion-query counts differ')
    inverted=set(); sr_pin={'FDRE':'R','FDSE':'S','FDCE':'CLR','FDPE':'PRE','LDCE':'CLR','AND2B1L':'SRI'}
    clk_pin={kind:('G' if kind=='LDCE' else None if kind=='AND2B1L' else 'C') for kind in sr_pin}
    for row in rows(directory/'ff_control_inversions.tsv'):
        name,kind,pin=row['cell'],row['primitive'],row['pin'];key=(name,pin)
        if expected.get(name)!=kind or pin not in (clk_pin.get(kind),sr_pin.get(kind)) or row['inverted']!='1' or key in inverted:
            raise ValueError('Invalid inverted FF control: '+name)
        inverted.add(key)
    with target.open('w') as f:
        f.write('AMF_FF_CONTROL_POLARITIES 1\n')
        for name,kind in sorted(expected.items()):
            f.write(f'{name} {kind} {int((name,clk_pin[kind]) in inverted)} {int((name,sr_pin[kind]) in inverted)} 0\n')
    return validate(target,cells_path)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('inputs','metadata','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    provenance=json.loads((a.inputs/'input_provenance.json').read_text())
    board=Path(provenance['input_directory'])
    binding=dict(line.split('\t') for line in (a.metadata/'binding.tsv').read_text().splitlines())
    if binding['input_dcp_sha256']!=provenance['input_dcp_sha256'] or digest(board/'prepared.dcp')!=binding['input_dcp_sha256']:
        raise ValueError('FF metadata and input checkpoint differ')
    for name in ('ff_control_counts.tsv','ff_control_inversions.tsv'):
        if digest(a.metadata/name)!=binding[name]:raise ValueError('FF metadata changed after export')
    if digest(board/'inventory/cells.tsv')!=provenance['inventory_files']['cells.tsv']:raise ValueError('Inventory changed')
    target=a.output.resolve()/'ff_controls.txt'
    report=materialize(a.metadata,board/'inventory/cells.tsv',target)
    cfg=json.loads((a.inputs/'config.json').read_text());cfg['FF control polarities file']=str(target)
    cfg['dumpDirectory']=str(a.output.resolve()/'unused-inspection-dump')
    provenance.update(parent_input_provenance={'path':str((a.inputs/'input_provenance.json').resolve()),'sha256':digest(a.inputs/'input_provenance.json')},
                      ff_control_polarities=report,ff_control_binding=binding,adapter_sha256=digest(__file__),
                      binary=None,binary_sha256=None)
    for name,value in [('config.json',cfg),('input_provenance.json',provenance),('ff_controls_summary.json',report)]:
        (a.output/name).write_text(json.dumps(value,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':main()
