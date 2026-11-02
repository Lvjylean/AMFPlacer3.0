"""Check the eight-cell boundary and external MC31 endpoints with the real initial packer."""
import argparse,csv,json,subprocess,time
from pathlib import Path
from check_resource_legalization import cell,archive
p=argparse.ArgumentParser()
for key in ('config','binary','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
base=json.loads(a.config.read_text())
for key in list(base):
    if key.startswith('ClockPeriod:') or key in ('fixed units file','fixed occupied sites file','clock file',
        'DSP registered outputs file','ASYNC_REG groups file','ASYNC_REG proximity pairs file',
        'FF control polarities file','SRL control metadata file'):
        base.pop(key)
base.update({'allow floating placement':'true','jobs':'1','CompositeIOTimingBoundary':'false'})
results=[]
for count in (8,9,17):
    w=a.output/str(count);w.mkdir();design=''
    for i in range(count):
        driver=f's{i-1}/Q31' if i else '@PORT/data'
        pins=[('D','IN',driver,driver),('CLK','IN','clk','@PORT/clk'),('CE','IN','one','<const1>'),('Q31','OUT',f's{i}/Q31',f's{i}/Q31')]
        pins += [(f'A[{b}]','IN','zero','<const0>') for b in range(5)]
        design+=cell(f's{i}','SRLC32E',pins)
    archive(w/'netlist.zip','allCellPinNet',design)
    cfg=dict(base,**{'vivado extracted design information file':str(w/'netlist.zip'),'dumpDirectory':str(w)})
    (w/'config.json').write_text(json.dumps(cfg,indent=2)+'\n');t=time.monotonic()
    with (w/'console.log').open('w') as f:
        run=subprocess.run([str(a.binary),str(w/'config.json'),'--inspect-packing',str(w/'packing.tsv')],stdout=f,stderr=subprocess.STDOUT,timeout=180)
    assert run.returncode==0,(count,(w/'console.log').read_text()[-2500:])
    rows={x['cell']:x for x in csv.DictReader((w/'packing.tsv').open(),delimiter='\t')}
    assert len(rows)==count
    for start in range(0,count,8):
        size=min(8,count-start)
        for index in range(size):
            row=rows[f's{start+index}'];assert row['macro']==f's{start}'
            assert row['bel']==chr(ord('A')+size-1-index)+'6LUT'
    results.append(dict(length=count,segments=(count+7)//8,wall_seconds=time.monotonic()-t))
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: 8/9/17-cell SRL chains; every segment ends at A6LUT')
