#!/usr/bin/env python3
"""One-shot GETRF DFX campaign. No retries and no modification of other jobs."""
import datetime,hashlib,json,os,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'experiments/evidence/getrf-dfx-large-01'
BINARY=Path('/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo/builds/build-20261013-122624-232823-4b31f6f6/build/AMFPlacer')
EXPECTED='90bf2dacce7e89dc53e299e11a8e401e64546155e5b845916df0c1d1d22a7cf3'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def save(p,obj):
    tmp=p.with_suffix('.tmp');tmp.write_text(json.dumps(obj,indent=2)+'\n');tmp.replace(p)
def memory():
    fields=dict(line.split(':',1) for line in Path('/proc/meminfo').read_text().splitlines())
    return int(fields['MemAvailable'].split()[0])/1024**2
def main():
    assert not (EVIDENCE/'campaign.json').exists(),'Campaign already launched'
    pre=json.loads((ROOT/'experiments/preflight/getrf-dfx-rm-synth-01/manifest.json').read_text())
    assert pre['state']=='completed' and pre['netlist_changed'] and pre['ports_identical']
    assert 'PASS controller equivalence 131072' in (EVIDENCE/'controller-equivalence.log').read_text()
    assert all(json.loads((EVIDENCE/'export-regression.json').read_text()).values())
    assert sha(BINARY)==EXPECTED
    names={'v0':'getrf-dfx-v0-shell-01','v1_full':'getrf-dfx-v1-full-01','v1_incremental':'getrf-dfx-v1-parallel-01'}
    for n in names.values():assert not (ROOT/'experiments/runs'/n).exists(),n
    record=dict(state='running',pid=os.getpid(),started_epoch=time.time(),
       started=datetime.datetime.now().astimezone().isoformat(),host=os.uname().nodename,
       source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
       binary=str(BINARY),binary_sha256=EXPECTED,resource_available_gib=memory(),
       scope='V0 shell + same V1 full Vivado reference + parallel RM synthesis/AMF/backend/assembly',jobs={})
    active={}
    def update():save(EVIDENCE/'campaign.json',record)
    def start(label,config,mode):
        out=ROOT/'experiments/runs'/names[label]
        cmd=[sys.executable,str(ROOT/'scripts/amf3.py'),'dfx-multi',mode,'--config',str(ROOT/'configs/experiments'/config),'--output',str(out)]
        if mode=='run':cmd+=['--binary',str(BINARY)]
        log=(EVIDENCE/(label+'-launcher.log')).open('xb')
        p=subprocess.Popen(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        log.close();active[label]=p
        record['jobs'][label]=dict(pid=p.pid,command=cmd,run=str(out),started_epoch=time.time(),state='running')
        update()
    def poll():
        for label,p in list(active.items()):
            rc=p.poll()
            if rc is not None:
                record['jobs'][label].update(exit_code=rc,ended_epoch=time.time(),state='completed' if rc==0 else 'failed')
                del active[label];update()
        with (EVIDENCE/'campaign-resources.jsonl').open('a') as f:
            f.write(json.dumps(dict(epoch=time.time(),available_gib=memory(),active_pids={k:p.pid for k,p in active.items()}))+'\n')
    update();start('v0','getrf-dfx-v0-full.json','shell')
    if memory()>=85:start('v1_full','getrf-dfx-v1-full.json','shell')
    while active:poll();time.sleep(30 if active else 0)
    if 'v1_full' not in record['jobs']:
        start('v1_full','getrf-dfx-v1-full.json','shell')
        while active:poll();time.sleep(30 if active else 0)
    if record['jobs']['v0']['exit_code']==0:
        start('v1_incremental','getrf-dfx-v1-incremental.json','run')
        while active:poll();time.sleep(30 if active else 0)
    else:record['incremental_blocked']='V0 shell failed; no incremental worker was launched.'
    record.update(state='completed' if len(record['jobs'])==3 and all(x.get('exit_code')==0 for x in record['jobs'].values()) else 'failed',
                  ended_epoch=time.time())
    record['wall_seconds']=record['ended_epoch']-record['started_epoch'];update()
    return 0 if record['state']=='completed' else 2
if __name__=='__main__':sys.exit(main())

