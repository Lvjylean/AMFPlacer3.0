"""Exercise real InitialPacker ownership and virtual FF reservations."""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import time
from check_resource_legalization import cell, archive

def run(args):
    root=args.root.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    base=json.loads((root/'configs/experiments/getrf-u250-full.json').read_text());base.pop('clock file',None)
    for k in list(base):
        if k.endswith(' file') or k=='mergedSharedCellType2sharedCellType':base[k]=str(root/base[k])
    base['jobs']='1'
    def ff(driver):
        return cell('sink','FDRE',[(p,'IN',n,d) for p,n,d in (
            ('C','clk','@PORT/clk'),('CE','one','<const1>'),('R','zero','<const0>'),('D','sum',driver))])
    route=cell('carry','CARRY8',[('O[2]','OUT','sum','carry/O[2]'),('CO[2]','OUT','co','carry/CO[2]')])+ff('carry/O[2]')
    route+=cell('load','LUT1',[('I0','IN','co','carry/CO[2]'),('O','OUT','result','load/O')])
    cascade=cell('carry','CARRY8',[('O[7]','OUT','sum','carry/O[7]'),('CO[7]','OUT','co','carry/CO[7]')])+ff('carry/O[7]')
    cascade+=cell('next','CARRY8',[('CI','IN','co','carry/CO[7]')])
    dynamic=''.join(cell(n,'LUT2',[('I0','IN','in','@PORT/in'),('O','OUT',n,n+'/O')]) for n in ('ci','di','select'))
    dynamic+=cell('carry','CARRY8',[('CI','IN','ci','ci/O'),('DI[0]','IN','di','di/O'),('S[0]','IN','select','select/O'),('CO[0]','OUT','co','carry/CO[0]')])
    dynamic+=cell('load','LUT1',[('I0','IN','co','carry/CO[0]'),('O','OUT','result','load/O')])
    results=[]
    for name,text in [('carry-route-through',route),('carry-dedicated-cascade',cascade),('carry-dynamic-ci',dynamic)]:
        d=out/name;d.mkdir();archive(d/'netlist.zip','allCellPinNet',text)
        cfg=dict(base,**{'vivado extracted design information file':str(d/'netlist.zip'),'dumpDirectory':str(d)})
        (d/'config.json').write_text(json.dumps(cfg,indent=2)+'\n');start=time.monotonic()
        with (d/'run.log').open('w') as log:
            subprocess.run([str(args.binary.resolve()),str(d/'config.json'),str(d/'ownership.tsv')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
        rows=list(csv.DictReader((d/'ownership.tsv').open(),delimiter='\t'))
        reserved={m[1] for row in rows if row['virtual']=='1' and row['ff']=='1'
                  for m in [re.search(r'__(FF2?[0-7])\(',row['cell'])] if m}
        if name=='carry-route-through':
            assert {f'FF{j}{i}' for i in range(4) for j in ('','2')} <= reserved,reserved
            assert next(r for r in rows if r['cell']=='sink')['is_carry']=='0',rows
        elif name=='carry-dedicated-cascade':
            assert next(r for r in rows if r['cell']=='sink')['is_carry']=='1',rows
            assert 'FF7' not in reserved,reserved
        else:
            assert 'FF0' in reserved,reserved
            assert 'FF4' not in reserved,reserved
        results.append(dict(case=name,reserved=sorted(reserved),seconds=time.monotonic()-start))
        (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(dict(passed=len(results),output=str(out))))

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for key in ('root','binary','output'):p.add_argument('--'+key,type=Path,required=True)
    run(p.parse_args())
