"""Exercise fabric-driven F7/F8 branches and sequential feedback through real BEL mapping."""
import argparse,json,subprocess,time
from pathlib import Path
from check_resource_legalization import cell,archive

p=argparse.ArgumentParser()
for key in ('config','binary','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
base=json.loads(a.config.read_text())
for key in list(base):
    if key.startswith('ClockPeriod:') or key in ('fixed units file','fixed occupied sites file','clock file',
        'DSP registered outputs file','ASYNC_REG groups file','ASYNC_REG proximity pairs file',
        'SRL control metadata file'):
        base.pop(key)
base.update({'allow floating placement':'true','jobs':'1','CompositeIOTimingBoundary':'false'})
def ff(name,driver='@PORT/data'):
    return cell(name,'FDRE',[('C','IN','clk','@PORT/clk'),('CE','IN','one','<const1>'),
        ('R','IN','zero','<const0>'),('D','IN',driver,driver),('Q','OUT',name+'/Q',name+'/Q')])
def mux(name,kind,x,y):
    return cell(name,kind,[('I0','IN',x,x),('I1','IN',y,y),('S','IN','select','@PORT/select'),
        ('O','OUT',name+'/O',name+'/O')])
cases={
    'f8-fabric':ff('a')+ff('b')+ff('c')+ff('sink','f8/O')+mux('f7','MUXF7','a/Q','b/Q')+mux('f8','MUXF8','f7/O','c/Q'),
    'f8-feedback':ff('a')+ff('b')+ff('c','f8/O')+mux('f7','MUXF7','a/Q','b/Q')+mux('f8','MUXF8','f7/O','c/Q'),
    'f7-fabric':ff('a')+ff('b')+ff('sink','f7/O')+mux('f7','MUXF7','a/Q','b/Q')}
results=[]
for name,design in cases.items():
    w=a.output/name;w.mkdir();archive(w/'netlist.zip','allCellPinNet',design)
    c=dict(base,**{'vivado extracted design information file':str(w/'netlist.zip'),'dumpDirectory':str(w)})
    (w/'config.json').write_text(json.dumps(c,indent=2)+'\n');t=time.monotonic()
    with (w/'console.log').open('w') as f:
        run=subprocess.run([str(a.binary),str(w/'config.json'),'--inspect-packing',str(w/'packing.tsv')],stdout=f,stderr=subprocess.STDOUT,timeout=180)
    results.append(dict(case=name,exit_code=run.returncode,wall_seconds=time.monotonic()-t))
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    assert run.returncode==0,(name,(w/'console.log').read_text()[-2500:])
    assert 'MUX cluster trial-commit checks: 1' in (w/'console.log').read_text()
print('PASS: fabric-driven F7/F8 and sequential-feedback BEL mapping')
