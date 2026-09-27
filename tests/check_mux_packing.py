#!/usr/bin/env python3
"""Exercise MUX/SRL ownership, primitive preservation and control compatibility in C++."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import time
from check_resource_legalization import cell, archive


def run(args):
    out=args.output.resolve(); out.mkdir(parents=True,exist_ok=False)
    root=args.root.resolve(); binary=args.binary.resolve()
    base=json.loads((root/'configs/experiments/getrf-u250-full.json').read_text()); base.pop('clock file',None)
    for k in list(base):
        if k.endswith(' file') or k=='mergedSharedCellType2sharedCellType': base[k]=str(root/base[k])
    base['jobs']='1'; checks=[]
    def netlist(kind='SRLC32E', mismatch=False, shared=False):
        text=''
        for name in ('a','b'):
            pins=[('Q' if kind=='SRLC32E' else 'O','OUT',name,name+('/Q' if kind=='SRLC32E' else '/O'))]
            if kind=='SRLC32E':
                clock='other' if mismatch and name=='b' else 'clk'
                pins += [('CLK','IN',clock,'@PORT/'+clock),('CE','IN','ce','@PORT/ce')]
            text+=cell(name,kind,pins)
        text+=cell('mux','MUXF7',[(p,'IN',n,n+('/Q' if kind=='SRLC32E' else '/O')) for p,n in [('I0','a'),('I1','a' if shared else 'b')]])
        return text
    mux8=''.join(cell(n,'LUT6',[('O','OUT',n,n+'/O')]) for n in ('a','b','c','d'))
    for n,a,b in [('low','a','b'),('high','c','d'),('root','high','low')]:
        mux8+=cell(n,'MUXF8' if n=='root' else 'MUXF7',[(p,'IN',x,x+'/O') for p,x in [('I0',a),('I1',b)]]+[('O','OUT',n,n+'/O')])
    for name, design, error in [('srl-f7',netlist(),None),('srl-controls',netlist(mismatch=True),'incompatible CLK/CE'),
                                ('srl-owned',netlist(shared=True),'already owned'),('lut-f7',netlist('LUT6'),None),('lut-f8',mux8,None),
                                ('srl-cascade-exit',cell('a','SRLC32E',[('Q31','OUT','cascade','a/Q31')])+cell('b','SRL16E',[('D','IN','cascade','a/Q31')]),None)]:
        d=out/name;d.mkdir();archive(d/'netlist.zip','allCellPinNet',design)
        cfg=dict(base,**{'vivado extracted design information file':str(d/'netlist.zip'),'dumpDirectory':str(d)})
        (d/'config.json').write_text(json.dumps(cfg));begin=time.monotonic()
        result=subprocess.run([str(binary),str(d/'config.json'),'--inspect-packing',str(d/'packing.tsv')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
        (d/'run.log').write_text(result.stdout)
        assert result.returncode==(2 if error else 0),(name,result.returncode,result.stdout[-3000:])
        if error: assert error in result.stdout
        elif name=='srl-f7':
            rows=list(csv.DictReader((d/'packing.tsv').open(),delimiter='\t'))
            assert {x['cell']:x['bel'] for x in rows}=={'a':'B6LUT','b':'A6LUT','mux':'F7MUX_AB'},rows
            assert all(x['slicem_required']=='1' for x in rows)
            assert rows[0]['primitive']==rows[1]['primitive'] and rows[0]['primitive']!=rows[2]['primitive']
        elif name=='srl-cascade-exit':
            rows=list(csv.DictReader((d/'packing.tsv').open(),delimiter='\t'))
            assert {x['cell']:x['bel'] for x in rows}=={'a':'B6LUT','b':'A6LUT'},rows
            assert all(x['slicem_required']=='1' for x in rows)
            assert len({x['macro'] for x in rows})==1
        elif name in ('lut-f7','lut-f8'):
            assert '#Mux Macro: 1' in result.stdout
            assert 'MUX cluster trial-commit checks: 1' in result.stdout
        checks.append(dict(name=name,exit_code=result.returncode,seconds=time.monotonic()-begin))
        (out/'results.json').write_text(json.dumps(checks,indent=2))
    print(json.dumps({'checks_passed':len(checks),'output':str(out)}))

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for k in ('root','binary','output'):p.add_argument('--'+k,type=Path,required=True)
    run(p.parse_args())
