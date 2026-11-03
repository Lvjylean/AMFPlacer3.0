"""Real native packing checks for LUT6_2/CARRY shared latch mode; no placement."""
import argparse
import csv
import json
from pathlib import Path
import resource
import subprocess
import time
from check_resource_legalization import cell, archive


def run(args):
    root = args.root.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cfg = json.loads((root / 'configs/experiments/getrf-u250-full.json').read_text())
    cfg.pop('clock file', None)
    for key in list(cfg):
        if key.endswith(' file') or key == 'mergedSharedCellType2sharedCellType':
            cfg[key] = str(root / cfg[key])
    cfg['jobs'] = '1'
    cells = {}
    expected = []
    def add(name, kind):
        cells[name] = [kind, []]
        if kind.startswith('LUT'):
            for i in range(6 if kind == 'LUT6_2' else int(kind[3:])):
                pin(name, 'I'+str(i), 'IN', '<const0>', '<const0>')
            for ref in (['O5', 'O6'] if kind == 'LUT6_2' else ['O']):
                pin(name, ref, 'OUT', name+'/'+ref, name+'/'+ref)
    def pin(name, ref, direction, net, driver):
        cells[name][1].append((ref, direction, net, driver))
    def connect(src, ref, dst, target):
        # CARRY output pins are introduced only when used.
        if not any(p[0] == ref and p[1] == 'OUT' for p in cells[src][1]):
            pin(src, ref, 'OUT', src+'/'+ref, src+'/'+ref)
        cells[dst][1] = [p for p in cells[dst][1] if p[0] != target]
        pin(dst, target, 'IN', src+'/'+ref, src+'/'+ref)
    def load(src, ref, dst):
        add(dst, 'LUT1'); connect(src, ref, dst, 'I0')
    def ff(src, ref, name):
        add(name, 'FDRE')
        for p,n,d in [('C','clk','@PORT/clk'),('CE','<const1>','<const1>'),('R','<const0>','<const0>')]:
            pin(name,p,'IN',n,d)
        connect(src,ref,name,'D')
    cases = [
        ('bottom',0,'fabric','O',True,False,False,True),
        ('top',5,'fabric','O',True,False,False,True),
        ('co_only',2,'fabric','CO',True,False,False,True),
        ('local_di',1,'di','O',True,False,False,False),
        ('unused_o5',2,'unused','O',True,False,False,False),
        ('registered_o',3,'fabric','O',False,True,False,False),
        ('registered_co',3,'fabric','CO',False,True,False,False),
        ('registered_and_fabric',1,'fabric','O',True,True,False,True),
        ('no_carry_output',2,'fabric','O',False,False,False,False),
        ('dedicated_cascade',7,'fabric','O',False,True,False,False),
        ('external_s',1,'fabric','O',True,False,True,False),
        ('cross_di',0,'cross_di','O',True,False,False,True),
    ]
    for name, bit, o5, output, fabric, registered, external_s, blocked in cases:
        c=name+'/c'; a=name+'/lut'; add(a,'LUT6_2'); add(c,'CARRY8')
        connect(a,'O6',c,'S['+str(bit)+']')
        if o5 in ('di','cross_di'):
            connect(a,'O5',c,'DI['+str(bit if o5=='di' else bit+1)+']')
        if o5 == 'fabric': load(a,'O5',name+'/o5_load')
        if external_s: load(a,'O6',name+'/o6_load')
        ref=output+'['+str(bit)+']'
        if fabric: load(c,ref,name+'/out_load')
        if registered: ff(c,ref,name+'/ff')
        if name == 'dedicated_cascade':
            add(name+'/next','CARRY8'); connect(c,'CO[7]',name+'/next','CI')
        expected.append(dict(case=name,carry=c,half=bit//4,blocked=blocked,registered=registered))
    archive(out/'netlist.zip','allCellPinNet',''.join(cell(n,k,p) for n,(k,p) in cells.items()))
    cfg.update({'vivado extracted design information file':str(out/'netlist.zip'),'dumpDirectory':str(out)})
    (out/'config.json').write_text(json.dumps(cfg,indent=2)+'\n')
    (out/'expectations.json').write_text(json.dumps(expected,indent=2)+'\n')
    def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    timings={}
    for name,binary,report in [('slots',args.binary,out/'carry'),('ownership',args.ownership_binary,out/'ownership.tsv')]:
        start=time.monotonic()
        with (out/(name+'.log')).open('w') as log:
            result=subprocess.run([str(binary.resolve()),str(out/'config.json'),str(report)],stdout=log,stderr=subprocess.STDOUT,
                                  timeout=180,preexec_fn=no_core)
        timings[name]=dict(seconds=time.monotonic()-start,exit_code=result.returncode)
        (out/'status.json').write_text(json.dumps(timings,indent=2)+'\n')
        assert result.returncode==0, str(out/(name+'.log'))
    slots={(r['carry'],int(r['half'])):r for r in csv.DictReader((out/'carry.ff-reservations.tsv').open(),delimiter='\t')}
    ownership={r['cell']:r for r in csv.DictReader((out/'ownership.tsv').open(),delimiter='\t')}
    for e in expected:
        r=slots[e['carry'],e['half']]
        assert (int(r['virtual_ff_mask'])==255)==e['blocked'],(e,r)
        assert int(slots[e['carry'],1-e['half']]['virtual_ff_mask'])!=255,(e,'other half reserved')
        if e['blocked']: assert int(r['real_ff_mask'])==0,(e,r)
        if e['registered']:
            assert (ownership[e['case']+'/ff']['is_carry']=='1')==(not e['blocked']),(e,ownership[e['case']+'/ff'])
    (out/'results.json').write_text(json.dumps(dict(passed=len(expected),cases=expected,timings=timings),indent=2)+'\n')
    print(json.dumps(dict(passed=len(expected),output=str(out))))

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    for key in ('root','binary','ownership-binary','output'):
        parser.add_argument('--'+key,type=Path,required=True)
    run(parser.parse_args())
