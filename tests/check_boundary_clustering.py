#!/usr/bin/env python3
"""Production C++ clustering on small constrained paths using the real U250 model."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from check_resource_legalization import archive,cell

def run(root,binary,out):
    out.mkdir(parents=True,exist_ok=False)
    base=json.loads((root/'configs/experiments/getrf-u250-physical-boundaries.json').read_text())
    base.pop('clock file',None)
    base.update(jobs='1',ClockPeriod='1',BoundaryMaxClusters='16')
    for k in list(base):
        if k.endswith(' file') or k=='mergedSharedCellType2sharedCellType':base[k]=str(root/base[k])
    results=[]
    for scenario in ('slr','io','xy','inside','dsp','uram'):
        d=out/scenario;d.mkdir()
        typ='DSP48E2' if scenario=='dsp' else 'URAM288' if scenario=='uram' else 'FDRE'
        pin='P[0]' if typ=='DSP48E2' else 'DOUT_A[0]' if typ=='URAM288' else 'Q'
        text=(cell('source',typ,[(pin,'OUT','a','source/'+pin)])+
              cell('mid','LUT1',[('I0','IN','a','source/'+pin),('O','OUT','b','mid/O')])+
              cell('sink','FDRE',[('D','IN','b','mid/O')]))
        archive(d/'netlist.zip','allCellPinNet',text)
        cfg=dict(base,**{'vivado extracted design information file':str(d/'netlist.zip'),'dumpDirectory':str(d),'BoundaryReportDirectory':str(d)})
        (d/'config.json').write_text(json.dumps(cfg,indent=2)+'\n')
        with (d/'run.log').open('w') as f:
            result=subprocess.run([str(binary),str(d/'config.json'),scenario,str(d/'result.tsv')],
                                  stdout=f,stderr=subprocess.STDOUT,env=dict(os.environ,OMP_NUM_THREADS='1'),timeout=180)
        if result.returncode:raise RuntimeError(scenario+' failed: '+(d/'run.log').read_text()[-4000:])
        results.append(dict(scenario=scenario,exit_code=result.returncode))
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results))
if __name__=='__main__':
    p=argparse.ArgumentParser()
    for n in ('root','binary','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.root.resolve(),a.binary.resolve(),a.output.resolve())

