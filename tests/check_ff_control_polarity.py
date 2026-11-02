"""Production input, cluster and BEL checks for FF control polarity."""
import argparse,json,subprocess
from pathlib import Path
from check_resource_legalization import cell,archive
p=argparse.ArgumentParser()
for key in ('config','binary','checker','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
c=json.loads(a.config.read_text())
for k in list(c):
    if k.startswith('ClockPeriod:') or k in ('fixed units file','fixed occupied sites file','clock file','DSP registered outputs file','ASYNC_REG groups file','ASYNC_REG proximity pairs file','SRL control metadata file','FF control polarities file'):c.pop(k)
records=[];design=''
for prefix,count,clock,sr,enable in [('normal',9,0,0,'@PORT/ce'),('inverted',8,0,1,'@PORT/ce'),('clock',1,1,0,'@PORT/ce'),('enable',1,0,0,'@PORT/other_ce')]:
    for i in range(count):
        n=prefix+str(i);design+=cell(n,'FDRE',[('C','IN','clk','@PORT/clk'),('R','IN','rst','@PORT/rst'),('CE','IN',enable,enable),('D','IN','d','@PORT/d'),('Q','OUT',n+'/Q',n+'/Q')]);records.append(f'{n} FDRE {clock} {sr} 0\n')
archive(a.output/'netlist.zip','allCellPinNet',design)
header='AMF_FF_CONTROL_POLARITIES 1\n';meta=a.output/'controls.txt';meta.write_text(header+''.join(records))
c.update({'allow floating placement':'true','CompositeIOTimingBoundary':'false','jobs':'1','dumpDirectory':str(a.output),'vivado extracted design information file':str(a.output/'netlist.zip'),'FF control polarities file':str(meta)})
cfg=a.output/'config.json';cfg.write_text(json.dumps(c))
results=[]
def run(name,command,code,phrase):
    with (a.output/(name+'.log')).open('w') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=180)
    text=(a.output/(name+'.log')).read_text();assert r.returncode==code and phrase in text,(name,r.returncode,text[-2000:]);results.append({'case':name,'exit_code':r.returncode})
run('packing',[str(a.checker),str(cfg)],0,'PASS full input')
for name,body,phrase in [('missing',records[:-1],'Missing FF control'),('duplicate',records+[records[0]],'duplicate'),('inverted-ce',[r.replace('normal0 FDRE 0 0 0','normal0 FDRE 0 0 1') for r in records],'unsupported FF control')]:
    file=a.output/(name+'.txt');file.write_text(header+''.join(body));cc=dict(c);cc['FF control polarities file']=str(file);cf=a.output/(name+'.json');cf.write_text(json.dumps(cc))
    run(name,[str(a.binary),str(cf),'--inspect-input',str(a.output/(name+'-report.json'))],2,phrase)
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS FF polarity packing and strict malformed-input guards')
