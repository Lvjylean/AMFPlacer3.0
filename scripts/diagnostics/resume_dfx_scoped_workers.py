#!/usr/bin/env python3
"""Resume the GETRF scoped-input export failure; preserve parent files and timings.

This diagnostic continuation consumes completed input tables, not a new extraction.
Only clock aliases are normalized using a read-only query of the same linked DCP.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from run_dfx import save, sha, tcl_word, placement_rows
from run_dfx_multi import snapshot, settings, vivado, acceptance, check_shell
from summarize_dfx import summarize
from validate_dfx_scoped_input import validate


def normalize_clocks(text, aliases):
    used = set()
    result = []
    for line in text.splitlines(keepends=True):
        marker = 'drivepin=> @DFX_CLOCK/'
        if marker in line:
            prefix, old = line.split('drivepin=> ', 1)
            old = old.rstrip('\n')
            if old not in aliases: raise ValueError('Missing clock alias: ' + old)
            used.add(aliases[old]); line = prefix + 'drivepin=> ' + aliases[old] + '\n'
        result.append(line)
    return ''.join(result), used


def run(args):
    parent = args.parent.resolve(); out = args.output.resolve(); binary = args.binary.resolve()
    previous = json.loads((parent/'manifest.json').read_text())
    if previous['state'] != 'failed': raise ValueError('Expected stopped failed parent')
    if json.loads(subprocess.check_output([str(binary), '--capabilities']))['dfx_incremental_schema'] != 1:
        raise ValueError('Not a DFX binary')
    build = json.loads((binary.parent.parent/'manifest.json').read_text())
    if build['binaries']['AMFPlacer'] != sha(binary): raise ValueError('Build hash mismatch')
    job = previous['job']; shell, _ = check_shell(job)
    probe = json.loads((args.aliases/'verified.json').read_text())
    out.mkdir(parents=True, exist_ok=False); scripts = snapshot(out); (out/'workers').mkdir()
    start = time.time()
    record = dict(schema='amf-dfx-scoped-input-resume-v1', state='running', job=job, pid=os.getpid(),
                  parent_run=str(parent), parent_manifest_sha256=sha(parent/'manifest.json'),
                  source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  script_hashes=scripts, binary=str(binary), binary_sha256=sha(binary), build=build,
                  started_epoch=start, workers={}, stages=[], dcp_storage='server-only',
                  prepare_rerun=False, rm_synthesis_rerun=False,
                  timing_note='Parent prepare and failed AMF costs are inherited separately, not hidden or charged twice.')
    shutil.copy2(__file__, out/'inputs'/Path(__file__).name)
    record['script_hashes'][Path(__file__).name] = sha(__file__)
    save(out/'manifest.json', record); save(out/'status.json', dict(state='running',stage='parallel_amf_vivado'))

    def worker(r):
        name=r['id']; source=parent/'workers'/name; target=out/'workers'/name
        old=json.loads((source/'manifest.json').read_text()); source_status=json.loads((source/'status.json').read_text())
        if source_status['state']=='running' or not any(s['name']=='prepare' and s['exit_code']==0 for s in old['stages']):
            raise ValueError('Input export incomplete: '+name)
        if 'Unassigned hard resource:' not in (source/'logs/amf.log').read_text():
            raise ValueError('Unexpected parent failure: '+name)
        for key,h in old['input_hashes'].items():
            if sha(old['job'][key])!=h: raise ValueError('Parent input changed: '+key)
        if sha(source/'linked.dcp')!=probe[name]['linked_sha256'] or sha(args.aliases/(name+'-aliases.tsv'))!=probe[name]['aliases_sha256']:
            raise ValueError('Clock-alias probe provenance mismatch')
        target.mkdir();(target/'logs').mkdir();(target/'placement').mkdir();(target/'inputs').mkdir();(target/'reports/physical').mkdir(parents=True)
        started=time.time();m=dict(schema='amf-dfx-run-v1',state='running',job=old['job'],stages=[],
            parent_run=str(source),parent_manifest_sha256=sha(source/'manifest.json'),input_hashes=old['input_hashes'],
            binary=str(binary),binary_sha256=sha(binary),build=build,source_commit=record['source_commit'],
            inherited_stages=old['stages'],input_recovery={},started_epoch=started,dcp_storage='server-only')
        def stage(label,command,cwd=None,env=None):
            save(target/'status.json',dict(state='running',stage=label));begin=time.time()
            with (target/'logs'/(label+'.log')).open('w') as log:
                rc=subprocess.run(command,cwd=cwd or target,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
            m['stages'].append(dict(name=label,command=command,seconds=time.time()-begin,started_epoch=begin,ended_epoch=time.time(),exit_code=rc));save(target/'manifest.json',m)
            if rc:raise RuntimeError(label+' failed '+str(rc))
        try:
            files=['linked.dcp','shell_boundary.tcldata','linked_boundary.tcldata','modeled_sites.txt','allowed_sites.txt','blocked_sites.txt',
                   'netlist.txt','cells.tsv','static_placement.tsv','srl_controls.tsv','clocks','dsp_registered_outputs.txt','dsp_certification.tsv','interface_context.tsv','input_scope.tsv']
            for f in files:
                shutil.copy2(source/f,target/f)
                m['input_recovery'][f]=dict(path=str(source/f),sha256=sha(source/f))
                if sha(target/f)!=m['input_recovery'][f]['sha256']:raise ValueError('Input copy mismatch '+f)
            aliases=dict(line.split('\t') for line in (args.aliases/(name+'-aliases.tsv')).read_text().splitlines())
            text,clocks=normalize_clocks((target/'netlist.txt').read_text(),aliases)
            (target/'netlist.txt').write_text(text);(target/'clocks').write_text('\n'.join(sorted(clocks))+'\n')
            m['clock_alias_normalization']=dict(probe=str(args.aliases),probe_sha256=sha(args.aliases/'verified.json'),aliases=len(aliases),canonical_clocks=len(clocks))
            m['input_validation']=validate(target);save(target/'input-validation.json',m['input_validation'])
            m['amf_input_scope']=dict(line.split('\t',1) for line in (target/'input_scope.tsv').read_text().splitlines())
            m['input_copy_normalization_seconds']=time.time()-started
            for n in ('dfx_common.tcl','dfx_backend.tcl','dfx_import.tcl'):shutil.copy2(ROOT/'scripts'/n,target/'inputs'/n)
            settings_path=target/'inputs/settings.tcl'
            values=dict(old['job'],output=str(target),modeled_sites=str(target/'modeled_sites.txt'))
            settings_path.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in values.items())+'\n')
            m['script_hashes']={p.name:sha(p) for p in (target/'inputs').iterdir()}
            cfg=json.loads((source/'amf_config.json').read_text())
            cfg={k:(v.replace(str(source),str(target)) if isinstance(v,str) else v) for k,v in cfg.items()}
            save(target/'amf_config.json',cfg)
            with zipfile.ZipFile(target/'netlist.zip','w',zipfile.ZIP_DEFLATED) as z:z.write(target/'netlist.txt','netlist')
            env=dict(os.environ,AMF_PROFILE_OUTPUT=str(target/'amf_profile.tsv'),AMF_GRID_UPDATE_MODE='batched')
            stage('amf',[str(binary),str(target/'amf_config.json')],target/'placement',env)
            with (target/'cells.tsv').open() as f:active={x['cell'] for x in csv.DictReader(f,delimiter='\t')}
            rows=placement_rows(target/'placement/DumpCLBPacking-first-0.tcl',active,set((target/'allowed_sites.txt').read_text().splitlines()),set((target/'blocked_sites.txt').read_text().splitlines()))
            (target/'placement.tsv').write_text(''.join(n+'\t'+v+'\n' for n,v in sorted(rows.items())))
            m['active_placement_coverage']=dict(expected=len(active),exported=len(rows),fraction=1.0)
            stage('backend',[job['vivado'],'-mode','batch','-source',str(target/'inputs/dfx_backend.tcl'),'-tclargs',str(settings_path)])
            names=['imported.dcp','placed.dcp','routed.dcp','rm_routed.dcp','pr_verify_passed.txt','full.bit','partial.bit']
            if any(not (target/n).is_file() or not (target/n).stat().st_size for n in names):raise RuntimeError('Missing backend artifact')
            m['acceptance']=summarize(target);save(target/'acceptance.json',m['acceptance'])
            if not m['acceptance']['implementation_verified']:raise RuntimeError('Backend acceptance failed')
            m['outputs']={n:dict(path=str(target/n),sha256=sha(target/n),bytes=(target/n).stat().st_size) for n in names};m['state']='completed'
        except Exception as error:
            m['state']='failed';m['error']=str(error)
        finally:
            m['total_wall_seconds']=time.time()-started;m['ended_epoch']=time.time();save(target/'manifest.json',m);save(target/'status.json',dict(state=m['state'],error=m.get('error')))
        return name,dict(state=m['state'],run=str(target),error=m.get('error'))

    try:
        with ThreadPoolExecutor(max_workers=2) as pool:
            for future in as_completed([pool.submit(worker,r) for r in job['partitions']]):
                name,result=future.result();record['workers'][name]=result;save(out/'manifest.json',record)
        if any(w['state']!='completed' for w in record['workers'].values()):raise RuntimeError('Worker failed; see manifests')
        assembly=out/'assembly';assembly.mkdir();(assembly/'inputs').mkdir();(assembly/'logs').mkdir()
        for n in ('dfx_multi_assemble.tcl','dfx_multi_common.tcl','dfx_common.tcl'):shutil.copy2(out/'inputs'/n,assembly/'inputs'/n)
        save(out/'status.json',dict(state='running',stage='assemble_routed_modules'))
        step=vivado(job,assembly,'dfx_multi_assemble.tcl',settings(job,assembly,dict(workers=str(out/'workers'),reference_checkpoint=str(shell/'base_routed.dcp'))));record['stages'].append(step)
        if step['exit_code']:raise RuntimeError('Routed assembly failed')
        record['acceptance']=acceptance(assembly,'assembled')
        record['acceptance']['pr_verify_full_check']=(assembly/'pr_verify_passed.txt').read_text().strip()=='full_check_passed'
        if not record['acceptance']['implementation_verified'] or not record['acceptance']['pr_verify_full_check']:raise RuntimeError('Assembly acceptance failed')
        record['outputs']={str(assembly/n):sha(assembly/n) for n in ['assembled_routed.dcp','full.bit']+[r['id']+'_partial.bit' for r in job['partitions']]}
        record['state']='completed'
    except Exception as error:
        record['state']='failed';record['error']=str(error)
    finally:
        record['wall_seconds']=time.time()-start;record['ended_epoch']=time.time();save(out/'manifest.json',record);save(out/'status.json',dict(state=record['state'],error=record.get('error')))
    return 0 if record['state']=='completed' else 1


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--parent',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--binary',type=Path,required=True);p.add_argument('--aliases',type=Path,required=True)
    raise SystemExit(run(p.parse_args()))
