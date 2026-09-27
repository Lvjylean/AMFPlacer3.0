"""Check whether Carry rules explain every Carry-related rejection site."""
import collections
import json
from pathlib import Path
import sys
import zipfile

run,evidence=map(Path,sys.argv[1:])
conflicts=json.loads((evidence/'conflicts.json').read_text())
bad={e['site'] for e in conflicts['events']}
assignments=dict(line.split('\t') for line in (run/'placement/requested.tsv').read_text().splitlines())
cells={name:dict(site=target.split('/')[0],bel=target.split('/')[1],pins={})
       for name,target in assignments.items() if target.split('/')[0] in bad}
fanout=collections.defaultdict(list)
cfg=json.loads((run/'config.json').read_text())
with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
    with z.open(z.namelist()[0]) as f:
        selected=None;name=kind=None
        for raw in f:
            if raw.startswith(b'curCell=> '):
                fields=raw.decode().split();name,kind=fields[1],fields[3]
                selected=cells.get(name)
                if selected is not None:selected['type']=kind
            elif selected is not None or b'/CO[' in raw or b'/O[' in raw:
                fields=raw.decode().split()
                if len(fields)!=10:continue
                ref,direction,net,driver=fields[3],fields[5],fields[7],fields[9]
                if selected is not None:selected['pins'][ref]=dict(direction=direction,net=net,driver=driver)
                if direction=='IN' and driver.rsplit('/',1)[0] in cells:
                    fanout[driver].append((name,kind,ref))
sites=collections.defaultdict(dict)
for name,c in cells.items():sites[c['site']][c['bel']]=name
findings=[]
for name,c in cells.items():
    if c['type']!='CARRY8':continue
    blocked_halves=set();bits=[]
    for bit in range(8):
        co=fanout[name+f'/CO[{bit}]'];o=fanout[name+f'/O[{bit}]']
        fabric=any(not (bit==7 and t=='CARRY8' and ref=='CI') for _,t,ref in co)
        if o and fabric:blocked_halves.add(bit//4);bits.append(bit)
    victims=[]
    for bel,member in sites[c['site']].items():
        if len(bel) in (3,4) and bel[1:] in ('FF','FF2') and (ord(bel[0])-65)//4 in blocked_halves:
            victims.append(member)
    if victims:findings.append(dict(site=c['site'],rule='OUTMUXC',bits=bits,affected_ffs=victims))
    ci=c['pins'].get('CI',{}).get('driver','')
    is_cascade=ci.endswith('/CO[7]') and ci.rsplit('/',1)[0] in assignments
    if ci and not ci.startswith('<const') and not is_cascade and 'AFF' in sites[c['site']]:
        ff=sites[c['site']]['AFF'];d=cells[ff]['pins'].get('D',{}).get('driver','')
        if d not in (name+'/O[0]',name+'/CO[0]'):
            findings.append(dict(site=c['site'],rule='FFMUXA1',ci_driver=ci,affected_ffs=[ff]))
summary={}
for rule in ('OUTMUXC','FFMUXA1'):
    expected={e['site'] for e in conflicts['events'] if e['category']==rule}
    explained={f['site'] for f in findings if f['rule']==rule}
    summary[rule]=dict(rejected_sites=len(expected),explained=len(expected&explained),unexplained=sorted(expected-explained))
(evidence/'carry_rule_coverage.json').write_text(json.dumps(dict(summary=summary,findings=findings),indent=2)+'\n')
print(json.dumps(summary,indent=2))
