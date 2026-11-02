"""Audit polarity collisions among the actual control-net identities in one frozen inventory."""
import argparse,csv,json,collections
from pathlib import Path
p=argparse.ArgumentParser()
for key in ('inventory','polarities','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();csv.field_size_limit(64*1024*1024)
ff={x['cell_id']:x for x in csv.DictReader((a.inventory/'cells.tsv').open(),delimiter='\t') if x['primitive'] in {'FDRE','FDSE','FDCE','FDPE'}}
pol={x['cell_name']:dict(clock_inverted='0',sr_inverted='0') for x in ff.values()}
assert sum(int(line.split()[1]) for line in (a.polarities.parent/'ff_counts.tsv').read_text().splitlines())==len(ff)
for row in csv.DictReader(a.polarities.open(),delimiter='\t'):
    assert row['cell'] in pol and row['inverted']=='1'
    pol[row['cell']]['clock_inverted' if row['pin']=='C' else 'sr_inverted']='1'
controls={i:{} for i in ff}
for row in csv.DictReader((a.inventory/'nets.tsv').open(),delimiter='\t'):
    for sink in row['sinks'].split(','):
        i,_,pin=sink.partition(':')
        if i in ff and pin in {'C','CE','R','S','CLR','PRE'}:
            controls[i][pin]=row['drivers']
sets=collections.defaultdict(lambda:collections.defaultdict(list))
for ident,cell in ff.items():
    c=controls[ident];q=pol[cell['cell_name']];sr=next((c[k] for k in ('R','S','CLR','PRE') if k in c),None)
    key=(c.get('C'),sr,cell['primitive'] in {'FDCE','FDPE'})
    sets[key][(q['clock_inverted'],q['sr_inverted'])].append(cell['cell_name'])
async_names={x['cell'] for x in csv.DictReader((a.inventory/'async_regs.tsv').open(),delimiter='\t')}
conflicts=[dict(clock=k[0],sr=k[1],asynchronous=k[2],polarities=[dict(clock=x[0],sr=x[1],count=len(names),async_members=sum(n in async_names for n in names),examples=names[:4]) for x,names in values.items()]) for k,values in sets.items() if len(values)>1]
result=dict(ff_count=len(ff),control_clock_sr_classes=len(sets),polarity_counts=dict(collections.Counter(q['clock_inverted']+','+q['sr_inverted'] for q in pol.values())),conflict_classes=len(conflicts),async_members_in_conflict_classes=sum(x['async_members'] for c in conflicts for x in c['polarities']),conflicts=conflicts,
    scope='This case only; absence of collisions does not establish general polarity-aware packing support.')
a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='conflicts'}))
