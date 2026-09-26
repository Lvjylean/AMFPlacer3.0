"""Recorded GETRF three-arm experiment; never changes the default configuration."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from inspect_amf_inputs import digest

VARIANTS={'control':'getrf-u250-physical-control.json','delay':'getrf-u250-physical-delay.json','cluster':'getrf-u250-physical-boundaries.json'}

def equivalent_configs(configs):
    def common(c):return {k:v for k,v in c.items() if not k.startswith('Boundary') and k!='PhysicalBoundaryMode'}
    base=common(configs['control'])
    if any(common(c)!=base for c in configs.values()):raise ValueError('Comparison inputs or common placement settings differ')
    return base

def collect(directory):
    state=json.loads((directory/'manifest.json').read_text());results={}
    for name in VARIANTS:
        item=state['variants'].get(name,{})
        run=Path(item['run']) if item.get('run') else None
        result=dict(item)
        if run and (run/'status.json').exists():result['status']=json.loads((run/'status.json').read_text())
        if run and (run/'reports/summary.json').exists():result['summary']=json.loads((run/'reports/summary.json').read_text())
        results[name]=result
    complete=all('exit_code' in x for x in results.values())
    report=dict(schema='physical-boundary-comparison-v1',state='completed' if complete else 'running',
        parallel=state['parallel'],runtime_caveat='Concurrent runs share CPU/memory; elapsed times are observed execution costs, not isolated speedup measurements.' if state['parallel']>1 else 'Runs scheduled serially; other server load is not controlled.',
        variants=results,default_changed=False,recommendation='retain-current-default-pending-complete-QoR')
    if complete:
        c=results['control'].get('summary',{});n=results['cluster'].get('summary',{})
        legal=all(v.get('summary',{}).get('implementation_verified',False) and v.get('summary',{}).get('drc_counts',{}).get('critical_warnings')==0 for v in results.values())
        improved=legal and n['timing']['wns_ns']>=c['timing']['wns_ns'] and n['timing']['tns_ns']>=c['timing']['tns_ns']
        report['recommendation']='candidate-passes-single-case-QoR-review' if improved else 'retain-current-default; new-strategy-not-validated-as-better'
    from amf3 import save
    save(directory/'comparison.json',report)
    return report

def worker(root,directory):
    from amf3 import save
    state=json.loads((directory/'manifest.json').read_text())
    def one(name):
        log=directory/(name+'.log')
        command=[sys.executable,str(root/'scripts/amf3.py'),'full-run','--config',str(directory/'configs'/VARIANTS[name]),'--binary',state['binary']]
        start=time.monotonic()
        with log.open('w') as f:
            process=subprocess.Popen(command,cwd=root,stdout=f,stderr=subprocess.STDOUT)
            code=process.wait()
        lines=log.read_text(errors='replace').splitlines()
        paths=[line for line in lines if line.startswith(str(root/'experiments/runs')+'/') and Path(line).is_dir()]
        return name,dict(run=paths[0] if paths else None,command=command,exit_code=code,elapsed_seconds=time.monotonic()-start)
    with ThreadPoolExecutor(max_workers=state['parallel']) as pool:
        futures=[pool.submit(one,name) for name in VARIANTS]
        for future in as_completed(futures):
            name,result=future.result();state['variants'][name]=result
            save(directory/'manifest.json',state);collect(directory)
    state['finished']=dt.datetime.now().astimezone().isoformat();save(directory/'manifest.json',state)
    collect(directory)

def launch(root,args):
    from amf3 import git,save,stamp
    import shutil
    root=Path(root);binary=(root/args.binary).resolve()
    if not binary.is_file():raise ValueError('Build the comparison binary first')
    configs={name:json.loads((root/'configs/experiments'/file).read_text()) for name,file in VARIANTS.items()}
    common=equivalent_configs(configs)
    from build_physical_boundaries import validate_model_inputs
    validate_model_inputs(root/common['physical boundary model file'],root/common['vivado extracted device information file'])
    directory=root/'experiments/comparisons'/('getrf-physical-'+stamp());(directory/'configs').mkdir(parents=True)
    for name,file in VARIANTS.items():save(directory/'configs'/file,configs[name])
    state=dict(schema='physical-boundary-comparison-v1',source_commit=git('rev-parse','HEAD'),git_status=git('status','--porcelain'),
        binary=str(binary),binary_sha256=digest(binary),parallel=args.parallel,
        started=dt.datetime.now().astimezone().isoformat(),variants={},clock_period_ns=common['ClockPeriod'],dcp_storage='server-only')
    shutil.copy2(Path(__file__),directory/'supervisor.py')
    save(directory/'manifest.json',state)
    with (directory/'supervisor.log').open('w') as log:
        process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--worker',str(root),str(directory)],cwd=root,
                                 stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    save(directory/'launcher.json',dict(pid=process.pid,directory=str(directory)))
    print(json.dumps(dict(pid=process.pid,directory=str(directory))))

if __name__=='__main__':
    if len(sys.argv)==4 and sys.argv[1]=='--worker':worker(Path(sys.argv[2]),Path(sys.argv[3]))
    else:raise SystemExit('Use amf3.py compare-boundaries')
