#!/usr/bin/env python3
"""Explicit one-time migration for completed full runs; never an incremental hot path."""
import argparse
import json
import os
from pathlib import Path
import shutil
import time
import zipfile
from parallel_runtime import command, verify_hashes
from common_static_contract import verify as verify_common
from native_cache_contract import seal_full, MODEL_KEYS
from run_dfx import save, sha, tcl_word, source_provenance


def run(config, output):
    job=json.loads(Path(config).read_text());out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    inputs=out/'inputs';inputs.mkdir();native=out/'native_cache';native.mkdir()
    for p in Path(__file__).parent.iterdir():
        if p.is_file() and p.suffix in ('.py','.tcl'):shutil.copy2(p,inputs/p.name)
    m=dict(state='running',scope='one-time historical migration; no synthesis/AMF placement/Vivado placement/routing',
           started_epoch=time.time(),job=job,stages=[],runner_pid=os.getpid(),script_hashes={p.name:sha(p) for p in inputs.iterdir()})
    save(out/'manifest.json',m)
    def stage(name,cmd,env=None):
        m['stage']=name;save(out/'manifest.json',m)
        result=command(cmd,out/(name+'-exec'),timeout=1800,env=env)
        m['stages'].append(dict(name=name,**result));save(out/'manifest.json',m)
    try:
        full=Path(job['full_input_run']);parent=json.loads((full/'manifest.json').read_text())
        if parent['state']!='completed' or not parent['acceptance']['implementation_verified'] or not parent['acceptance']['timing_met']:
            raise ValueError('Historical full reference is not accepted')
        reference=Path(job['reference_checkpoint']);binary=Path(job['binary'])
        if sha(reference)!=parent['outputs']['base_routed.dcp']:raise ValueError('Full DCP identity mismatch')
        provenance=source_provenance(binary);m['build']=provenance
        common=verify_common(parent['job'],job['common_static_run'],reference)
        cfg=json.loads((full/'amf_config.json').read_text())
        for key in MODEL_KEYS:
            if key not in cfg:continue
            cfg[key]=str(Path(job['model_directory'])/Path(cfg[key]).name)
            if sha(cfg[key])!=parent['input_hashes'][key]:raise ValueError('Historical device/model mismatch: '+key)
        probe=Path(job['interface_run']);pm=json.loads((probe/'manifest.json').read_text())
        if pm['state']!='completed' or not pm['source_unchanged'] or pm['source_sha256_before']!=sha(reference):
            raise ValueError('Full interface metadata is not certified for reference')
        for name in ('native_ports.tsv','native_boundary_pins.tsv'):
            if sha(probe/name)!=pm['outputs'][name]:raise ValueError('Interface metadata changed')
            os.symlink(probe/name,out/name)
        os.symlink(reference,out/'base_routed.dcp')
        os.symlink(Path(job['common_static_run'])/'common_static_locked.dcp',out/'common_static_locked.dcp')
        os.symlink(full/'routed_retention.tsv',out/'routed_retention.tsv')
        cfg.update({'Native cache action':'bootstrap-existing-full','Native static cache write':str(native/'proposed.bin'),
                    'Native static identity':job['native_static_identity'],'Native full input sha256':sha(full/'netlist.zip'),
                    'Native source sha256':sha(binary),'Native historical full netlist':str(out/'netlist.txt'),
                    'Native historical placement Tcl':str(full/'placement/DumpCLBPacking-first-0.tcl'),
                    'Native full ports file':str(out/'native_ports.tsv'),'Native boundary pins file':str(out/'native_boundary_pins.tsv'),
                    'Native dedicated interfaces file':str(full/'dedicated_interface_pins.tsv')})
        for key in ('DFX regions file','DFX region sites file','SRL control metadata file','DSP registered outputs file'):
            cfg[key]=str(full/Path(cfg[key]).name)
            if sha(cfg[key])!=parent['input_hashes'][key]:raise ValueError('Historical full metadata changed: '+key)
        if sha(full/'netlist.zip')!=parent['input_hashes']['vivado extracted design information file']:
            raise ValueError('Historical full netlist hash mismatch')
        tracked=[reference,binary,full/'manifest.json',full/'netlist.zip',full/'routed_retention.tsv',
                 full/'placement/DumpCLBPacking-first-0.tcl',full/'dedicated_interface_pins.tsv',
                 Path(job['common_static_run'])/'common_static_locked.dcp',probe/'manifest.json']
        tracked += [Path(v) for k,v in cfg.items() if ('file' in k or k in MODEL_KEYS) and Path(v).is_file()]
        expected={str(p):sha(p) for p in tracked};m['input_hashes']=expected;save(out/'manifest.json',m)
        if job.get('recover_capture'):
            recovery=job['recover_capture'];old=Path(recovery['run'])
            verify_hashes(recovery['hashes'])
            previous=json.loads((old/'manifest.json').read_text())
            if previous['state']!='failed':raise ValueError('Capture recovery parent must have stopped')
            for key,value in previous['job'].items():
                if key!='recover_capture' and job.get(key)!=value:raise ValueError('Capture recovery identity mismatch: '+key)
            verify_hashes(previous['input_hashes'])
            inherited=[]
            for name in ('bootstrap','context_spec'):
                stages=[s for s in previous['stages'] if s['name']==name and s.get('exit_code')==0]
                if len(stages)!=1:raise ValueError('Missing successful capture producer: '+name)
                inherited.extend(stages)
            required=[old/'manifest.json',old/'native_cache/proposed.bin',old/'native_cache/context-spec.tsv']
            if any(str(p) not in recovery['hashes'] for p in required):raise ValueError('Unbound capture recovery artifact')
            for name in ('proposed.bin','context-spec.tsv'):os.symlink(old/'native_cache'/name,native/name)
            m.update(capture_reused=True,capture_recovery=recovery,inherited_stages=inherited,
                     inherited_parent_wall_seconds=previous['wall_seconds'],
                     inherited_netlist_decompression_seconds=previous['netlist_decompression_seconds'])
            expected.update(recovery['hashes']);save(out/'manifest.json',m)
        else:
            t=time.monotonic()
            with zipfile.ZipFile(full/'netlist.zip') as z:
                names=[n for n in z.namelist() if not n.endswith('/')]
                if len(names)!=1:raise ValueError('Ambiguous historical netlist archive')
                with z.open(names[0]) as src,(out/'netlist.txt').open('xb') as dest:shutil.copyfileobj(src,dest,1024*1024)
            m['netlist_decompression_seconds']=time.monotonic()-t
            save(out/'bootstrap.json',cfg);stage('bootstrap',[str(binary),'--native-cache',str(out/'bootstrap.json')])
            audit={'Native cache action':'audit-spec','Native static cache proposal':str(native/'proposed.bin'),
                   'Native context audit specification':str(native/'context-spec.tsv')}
            save(native/'audit-spec.json',audit);stage('context_spec',[str(binary),'--native-cache',str(native/'audit-spec.json')])
        settings={'output':str(out),'reference_checkpoint':str(reference),'regions_file':cfg['DFX regions file'],'vivado_threads':4}
        settings['native_resource_inventory']=int(job.get('native_dfx_edge_policy','off')!='off')
        sp=inputs/'settings.tcl';sp.write_text(''.join('set dfx('+k+') '+tcl_word(v)+'\n' for k,v in settings.items()))
        stage('certify',[job['vivado'],'-mode','batch','-source',str(inputs/'dfx_native_cert_probe.tcl'),'-tclargs',str(sp)])
        sealjob=dict(parent['job'],native_coordinate_map=job['native_coordinate_map'],
                     native_dfx_edge_policy=job.get('native_dfx_edge_policy','off'),
                     native_dfx_edge_side_policy=job.get('native_dfx_edge_side_policy','interior-prohibit'))
        m['native_static_cache']=seal_full(sealjob,out,binary,cfg,stage,parent['acceptance'],common)
        verify_hashes(expected)
        m.update(state='completed',sources_unchanged=True)
    except Exception as e:
        m.update(state='failed',error=repr(e))
    finally:
        m['finished_epoch']=time.time();m['wall_seconds']=m['finished_epoch']-m['started_epoch'];save(out/'manifest.json',m)
    return m['state']=='completed'

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();raise SystemExit(0 if run(a.config,a.output) else 1)
