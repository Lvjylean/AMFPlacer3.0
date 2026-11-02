"""The interface-only probe must never make unsupported full URAM placement look legal."""
import argparse,json,subprocess
from pathlib import Path
from check_resource_legalization import cell,archive
p=argparse.ArgumentParser()
for key in ('config','binary','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
cfg=json.loads(a.config.read_text())
for key in list(cfg):
    if key.startswith('ClockPeriod:') or key in ('fixed units file','fixed occupied sites file','clock file','DSP registered outputs file','ASYNC_REG groups file','ASYNC_REG proximity pairs file','SRL control metadata file'):cfg.pop(key)
text=cell('a','URAM288',[('CAS_OUT_ADDR_A[0]','OUT','cascade','a/CAS_OUT_ADDR_A[0]')])+cell('b','URAM288',[('CAS_IN_ADDR_A[0]','IN','cascade','a/CAS_OUT_ADDR_A[0]')])
archive(a.output/'netlist.zip','allCellPinNet',text)
cfg.update({'allow floating placement':'true','jobs':'1','CompositeIOTimingBoundary':'true','dumpDirectory':str(a.output),'vivado extracted design information file':str(a.output/'netlist.zip')})
(a.output/'config.json').write_text(json.dumps(cfg,indent=2)+'\n')
results=[]
for mode,code in [('inspect-packing',2),('inspect-interface-adaptation',0)]:
    with (a.output/(mode+'.log')).open('w') as f:
        result=subprocess.run([str(a.binary),str(a.output/'config.json'),'--'+mode,str(a.output/(mode+'.tsv'))],stdout=f,stderr=subprocess.STDOUT,timeout=180)
    assert result.returncode==code,(mode,result.returncode)
    assert 'URAM cascade is not supported' in (a.output/(mode+'.log')).read_text()
    results.append(dict(mode=mode,exit_code=result.returncode,expected=code))
assert 'full_design_legalization_ready\t0' in (a.output/'inspect-interface-adaptation.tsv.scope.tsv').read_text()
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: full packing rejects URAM cascade; interface probe labels its narrower scope')
