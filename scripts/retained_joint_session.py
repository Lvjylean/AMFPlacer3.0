"""The mandatory static shell is opened once and retained through joint route."""
import copy,json,os,signal,subprocess,sys,time,uuid,shutil
from pathlib import Path
from run_dfx import save,sha,tcl_word
import run_dfx_multi as flow

class RetainedJointSession:
    def __init__(self,job,out,scripts,environment):
        self.job=copy.deepcopy(job);self.out=Path(out);self.scripts=Path(scripts);self.environment=environment
        self.joint=self.out/'joint';self.inputs=self.joint/'inputs';self.inputs.mkdir(parents=True)
        self.directory=self.out/'retained_session';self.directory.mkdir()
        self.token=uuid.uuid4().hex;self.process=None;self.worker=None;self.log=None
        self.record={'state':'preparing','token':self.token,'ordinary_open_count':1,'added_budget_seconds':180}
        for p in self.scripts.glob('*.tcl'):shutil.copy2(p,self.inputs/p.name)
        metadata=self.inputs/'common_static';metadata.mkdir()
        common=Path(job['common_static_shell_run'])
        for p in job['partitions']:
            k=p['id'];shutil.copy2(common/(k+'_common_static_boundary.tcldata'),metadata/(k+'_common_static_boundary.tcldata'))
        extra=dict(common_static_checkpoint=str(common/'common_static_locked.dcp'),common_static_metadata=str(metadata),
                   reference_checkpoint=str(Path(job['shell_run'])/'base_routed.dcp'),
                   same_session_directory=str(self.directory),same_session_token=self.token,
                   same_session_occupancy=job['same_session_cache']['occupancy'])
        for p in job['partitions']:
            k=p['id'];module=self.out/'branches'/k/'module'
            extra.update({'worker_'+k:str(module),'rm_'+k+'_checkpoint':str(module/'rm_amf_placed.dcp')})
        self.settings=flow.settings(dict(job,native_local_pin_access_policy='fixed-static-local-entrance-v2'),self.joint,extra)
        self.execdir=self.joint/'exec';self.execdir.mkdir()
    def save(self):save(self.directory/'status.json',self.record)
    def start_and_certify(self):
        self.started=time.monotonic();self.started_epoch=time.time()
        self.log=(self.execdir/'process.log').open('w')
        argv=[self.job['vivado'],'-mode','batch','-source',str(self.inputs/'dfx_joint_backend.tcl'),
              '-tclargs',str(self.settings)]
        self.process=subprocess.Popen(argv,cwd=self.execdir,env=self.environment,stdout=self.log,
                                      stderr=subprocess.STDOUT,start_new_session=True)
        self.record.update(state='opening_static',pid=self.process.pid,process_group=self.process.pid,
                           started_epoch=self.started_epoch,command=argv)
        self.save();save(self.execdir/'process.json',self.record)
        marker=self.wait('cache_started',time.monotonic()+2400)
        # Tcl's start timestamp includes ownership query time even if the parent
        # observes the marker after a scheduling delay.
        elapsed=max(0,time.time()-float(marker['epoch_ms'])/1000)
        self.deadline=time.monotonic()+180-elapsed
        self.record.update(state='cache_enrichment',cache_started_epoch=float(marker['epoch_ms'])/1000);self.save()
        cfg=copy.deepcopy(self.job['same_session_cache'])
        cfg.update(parent_cache=self.job['native_static_cache']['path'],
                   parent_contract=self.job['native_static_cache']['contract'],
                   static_shell=str(Path(self.job['common_static_shell_run'])/'common_static_locked.dcp'),
                   owners=str(self.directory/'owners.tsv'),binary=self.job['amf_binary'],
                   clock_period=str(self.job['clock_period_ns']),
                   owner_marker=str(self.directory/'owners_ready'),session_token=self.token)
        save(self.directory/'cache-config.json',cfg)
        command=[sys.executable,str(self.scripts/'certify_local_pin_access.py'),'--config',str(self.directory/'cache-config.json'),
                 '--output',str(self.directory/'cache'),'--deadline',str(self.deadline)]
        with (self.directory/'cache-worker.log').open('w') as log:
            self.worker=subprocess.Popen(command,env=self.environment,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            self.record.update(cache_worker_pid=self.worker.pid);self.save()
            while self.worker.poll() is None:
                if self.process.poll() is not None:raise RuntimeError('Retained Vivado exited during cache generation')
                if time.monotonic()>=self.deadline:raise TimeoutError('Added cache work exceeded180s')
                time.sleep(.05)
            code=self.worker.returncode
        if code:raise RuntimeError('Local cache generation failed; see cache-worker.log')
        result=json.loads((self.directory/'cache/generation.json').read_text())
        added=180-(self.deadline-time.monotonic())
        if added>=180:raise TimeoutError('Added cache publication exceeded180s')
        if result['state']!='completed':raise ValueError('Uncertified local cache')
        self.job['native_static_cache']=result['native_static_cache']
        self.job['native_local_pin_access_policy']='fixed-static-local-entrance-v2'
        self.record.update(state='awaiting_modules',cache=result,added_cache_seconds=added,
                           cache_finished_epoch=time.time(),extra_open_seconds=0)
        self.save()
        self.publish('cache_ready',{'added_seconds':added})
        # Publication is part of the added work. A late publication never
        # authorizes starting modules or extending the agreed budget.
        added=180-(self.deadline-time.monotonic())
        if added>=180:raise TimeoutError('Added cache publication exceeded180s')
        self.record['added_cache_seconds']=added
        self.record['budget_checked_after_publication']=True
        self.save()
        return self.job,self.record
    def publish(self,name,values):
        values=dict(token=self.token,**values)
        p=self.directory/name;t=p.with_suffix('.tmp')
        t.write_text(' '.join(tcl_word(str(x)) for pair in values.items() for x in pair)+'\n');t.replace(p)
    def wait(self,name,deadline):
        p=self.directory/name
        while not p.is_file():
            if self.process.poll() is not None:raise RuntimeError('Retained Vivado exited before '+name)
            if time.monotonic()>=deadline:raise TimeoutError('Deadline waiting for '+name)
            time.sleep(.1)
        # Marker keys/values contain no Tcl substitutions and are deliberately
        # limited to single-token identifiers/numeric values.
        fields=p.read_text().split()
        if len(fields)%2:raise ValueError('Malformed session marker')
        values=dict(zip(fields[::2],fields[1::2]))
        if values.get('token')!=self.token:raise ValueError('Session identity mismatch')
        return values
    def finish(self,workers):
        self.record.update(state='import_and_route',modules_ready_epoch=time.time());self.save()
        self.publish('modules_ready',{'cells_'+k:w['cells'] for k,w in workers.items()})
        try:code=self.process.wait(timeout=14400)
        except subprocess.TimeoutExpired:raise TimeoutError('Joint backend deadline exceeded')
        self.log.close()
        result=dict(command=self.record['command'],pid=self.process.pid,process_group=self.process.pid,
                    started_epoch=self.started_epoch,ended_epoch=time.time(),
                    seconds=time.monotonic()-self.started,exit_code=code,state='completed' if code==0 else 'failed',
                    ordinary_open_included=True,cache_and_module_wait_included=True)
        self.record.update(state=result['state'],process=result);self.save();save(self.execdir/'process.json',result)
        if code:raise RuntimeError('Retained joint backend failed; see process.log')
        return result
    def abort(self,reason):
        (self.directory/'abort').write_text(str(reason)+'\n')
        for child in (self.worker,self.process):
            if child is not None and child.poll() is None:
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=10)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
        if self.log and not self.log.closed:self.log.close()
        self.record.update(state='failed',error=str(reason),ended_epoch=time.time());self.save()

