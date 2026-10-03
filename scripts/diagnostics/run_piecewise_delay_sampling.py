#!/usr/bin/env python3
"""Recorded, read-only sampling of existing routed U250 checkpoints on eda072."""
import argparse
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import threading
from concurrent.futures import ThreadPoolExecutor

from prepare_sa_ratio_samples import digest

ROOT=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',action='append',required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--per-stratum',type=int,default=10)
    p.add_argument('--parallel',type=int,choices=(1,2,3,4),default=1)
    p.add_argument('--shards',type=int,choices=(1,2),default=1)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    machine=json.loads((ROOT/'configs/machines/eda072.json').read_text())
    if str(ROOT)!=machine['project_root']: raise RuntimeError('Run on eda072')
    scripts=out/'scripts';scripts.mkdir()
    for name in ('run_piecewise_delay_sampling.py','prepare_piecewise_delay_samples.py','export_sa_ratio_samples.tcl','analyze_boundary_timing_samples.py'):
        shutil.copy2(ROOT/'scripts/diagnostics'/name,scripts/name)
    record=dict(schema='piecewise-delay-sampling-run-v1',started=dt.datetime.now().astimezone().isoformat(),
                source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                git_status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True),
                state='running',stages=[],inputs={},scripts={f.name:digest(f) for f in scripts.iterdir()},
                dcp_storage='server-only',mutation='none: open checkpoint and query net delays',parallel=a.parallel,shards=a.shards)
    lock=threading.Lock()
    def save(): (out/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
    def stage(name,cmd):
        print(name,flush=True);start=time.monotonic()
        with lock:
            record.setdefault('active_stages',[]).append(name);save()
        with (out/(name+'.log')).open('w') as f:
            r=subprocess.run(list(map(str,cmd)),cwd=out,stdout=f,stderr=subprocess.STDOUT)
        with lock:
            record['stages'].append(dict(name=name,command=list(map(str,cmd)),elapsed_seconds=time.monotonic()-start,exit_code=r.returncode))
            record['active_stages'].remove(name);save()
        if r.returncode: raise RuntimeError(name+' failed')
    try:
        model=ROOT/'data/devices/u250-physical-v1/model/physical_structure.tsv'
        netlist=ROOT/'data/reference/getrf-u250/amf-inputs/netlist.zip'
        for f in (model,netlist): record['inputs'][str(f)]=digest(f)
        jobs=[];datasets=[]
        for i,name in enumerate(a.run):
            run=(ROOT/'experiments/runs'/name).resolve()
            if run.parent!=ROOT/'experiments/runs': raise ValueError('Invalid run ID')
            dcp=run/'reports/getrf_routed.dcp';loc=run/'placement/routed_cell_sites.tsv'
            for f in (dcp,loc,run/'manifest.json',run/'config.json'): record['inputs'][str(f)]=digest(f)
            dest=out/f'layout-{i}'
            stage(f'prepare-{i}',[sys.executable,scripts/'prepare_piecewise_delay_samples.py','--model',model,'--locations',loc,'--netlist',netlist,'--output',dest,'--per-stratum',a.per_stratum])
            request_lines=(dest/'requests.tsv').read_text().splitlines()
            datasets.append(dest)
            for shard in range(a.shards):
                target=dest if a.shards==1 else dest/f'shard-{shard}'
                if a.shards>1:
                    target.mkdir();(target/'requests.tsv').write_text('\n'.join([request_lines[0],*request_lines[1+shard::a.shards]])+'\n')
                record['inputs'][str(target/'requests.tsv')]=digest(target/'requests.tsv')
                jobs.append((f'vivado-{i}-{shard}',[machine['vivado'],'-mode','batch','-notrace','-nojournal','-log',target/'vivado.log','-source',scripts/'export_sa_ratio_samples.tcl','-tclargs',dcp,target/'requests.tsv',target]))
        with ThreadPoolExecutor(max_workers=a.parallel) as pool:
            futures=[pool.submit(stage,*job) for job in jobs]
            for future in futures: future.result()
        if a.shards>1:
            for dest in datasets:
                for name in ('samples.tsv','gaps.tsv'):
                    header=None
                    with (dest/name).open('w') as output:
                        for shard in range(a.shards):
                            lines=(dest/f'shard-{shard}'/name).read_text().splitlines()
                            if header is None: header=lines[0];output.write(header+'\n')
                            if lines[0]!=header: raise ValueError('Shard header mismatch')
                            for line in lines[1:]:output.write(line+'\n')
        record['state']='completed'
    except Exception as exc:
        record['state']='failed';record['error']=str(exc);raise
    finally:
        record['finished']=dt.datetime.now().astimezone().isoformat();record.pop('active_stages',None);save()


if __name__=='__main__': main()
