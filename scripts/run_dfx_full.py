#!/usr/bin/env python3
"""Whole-design AMF placement under hard DFX regions, then full Vivado routing."""
from common_static_contract import record as record_common_static, artifact_names as common_static_artifacts
import argparse
import csv
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile
from run_dfx import amf_configuration, placement_rows, sha, save
from run_dfx_multi import load, settings, acceptance, static_containment_enabled
from dfx_comparison import identity
from verify_full_export_prefix import verify as verify_export_prefix
from dfx_reuse_prepared import reuse, verify_srl_refresh


def run(args):
    setup_begin=time.monotonic()
    job=load(args.config);cfg=amf_configuration(job)
    binary=Path(args.binary).resolve()
    helper=binary.parent/'partitionHyperGraph'
    if sha(helper)!=job['runtime_helper_sha256'] or not os.access(helper,os.X_OK):raise ValueError('Graph partition helper absent/changed/not executable')
    capability=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True))
    if capability.get('dfx_full_hard_regions_schema')!=1:raise ValueError('Binary lacks whole-design hard DFX regions')
    from native_cache_contract import validate_full_capture
    validate_full_capture(job,capability,cfg)
    if job.get('resume_synthesis_checkpoint'):
        if sha(Path(job['resume_synthesis_checkpoint']))!=job['synthesis_sha256']:raise ValueError('Synthesis input hash mismatch')
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    for p in ('inputs','logs','placement','reports/physical'):(out/p).mkdir(parents=True)
    script=Path(__file__).resolve().parent
    for f in script.iterdir():
        if f.is_file() and f.suffix in ('.py','.tcl'):shutil.copy2(f,out/'inputs'/f.name)
    revision=subprocess.run(['git','rev-parse','HEAD'],cwd=script.parent,text=True,capture_output=True)
    m=dict(schema='amf-dfx-multi-shell-v1',state='running',job=job,stages=[],started_epoch=time.time(),
           amf_placement_scope='whole_design',binary=str(binary),binary_sha256=sha(binary),
           runner_source_commit=revision.stdout.strip() if revision.returncode==0 else None,
           script_hashes={f.name:sha(f) for f in (out/'inputs').iterdir()},dcp_storage='server-only')
    build=binary.parent.parent/'manifest.json'
    if not build.is_file():raise ValueError('Missing frozen build manifest')
    m['comparison_identity']=identity(job,m['binary_sha256'],cfg) if job.get('comparison_role')=='v1_full' else None
    if m['comparison_identity'] and job.get('comparison_role')=='v1_full':
        if not job.get('resume_synthesis_checkpoint') or sha(Path(job['resume_synthesis_checkpoint']))!=m['comparison_identity']['v1_design_input_sha256']:
            raise ValueError('V1 full reference must actually compile the bound V1 checkpoint')
    m['build']=json.loads(build.read_text())
    m['source_commit']=m['build']['source_commit']
    if m['build'].get('binaries',{}).get('AMFPlacer')!=m['binary_sha256']:raise ValueError('Frozen build hash mismatch')
    m['source_inputs']={}
    for group in ('rtl_sources','ip_scripts','xdc_files','assembly_xdc_files'):
        for name in job.get(group,[]):
            path=Path(name)
            if not path.is_file():
                if not job.get('resume_synthesis_checkpoint'):raise ValueError('Missing source input: '+name)
                continue
            dest=out/'inputs/source'/('%04d-'%len(m['source_inputs'])+path.name)
            dest.parent.mkdir(exist_ok=True);shutil.copy2(path,dest)
            m['source_inputs'][name]=dict(sha256=sha(dest),snapshot=str(dest.relative_to(out)))
    m['preparation_timings']={'runner_setup_seconds':time.monotonic()-setup_begin}
    def measured(name,operation):
        begin=time.monotonic()
        try:return operation()
        finally:
            m['preparation_timings'][name]=time.monotonic()-begin
            save(out/'preparation_profile.json',m['preparation_timings'])
    def update(stage):
        save(out/'manifest.json',m);save(out/'status.json',dict(state=m['state'],stage=stage,error=m.get('error')))
    def stage(name,cmd,env=None):
        update(name);started=time.time();begin=time.monotonic()
        with (out/'logs'/(name+'.log')).open('w') as log:
            p=subprocess.Popen(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,env=env)
            m['child_pid']=p.pid;update(name);rc=p.wait()
        m['stages'].append(dict(name=name,command=cmd,exit_code=rc,seconds=time.monotonic()-begin,started_epoch=started,ended_epoch=time.time()))
        update(name)
        if rc:raise RuntimeError(name+' failed; preserve this run')
    try:
        device=Path(cfg['vivado extracted device information file'])
        with zipfile.ZipFile(device) as z:
            names=[n for n in z.namelist() if not n.endswith('/')]
            if len(names)!=1:raise ValueError('Ambiguous device archive')
            sites=set(re.findall(r'^site=> (\S+)',z.read(names[0]).decode(),re.M))
        (out/'modeled_sites.txt').write_text('\n'.join(sorted(sites))+'\n')
        if job.get('prepare_from_full_run'):
            parent=Path(job['prepare_from_full_run']);pm=json.loads((parent/'manifest.json').read_text())
            if pm.get('state') not in ('failed','canceled','completed'):raise ValueError('Preparation parent is still active')
            if pm.get('amf_placement_scope')!='whole_design':raise ValueError('Preparation parent lacks full-design scope')
            if static_containment_enabled(job) != static_containment_enabled(pm['job'], historical_default=True):
                raise ValueError('Preparation parent static routing policy mismatch')
            if static_containment_enabled(job) and job.get('static_region_granularity', 'site') != pm['job'].get('static_region_granularity', 'site'):
                raise ValueError('Preparation parent static region granularity mismatch')
            for key in ('part','top','clock_name','clock_period_ns','partitions','resume_synthesis_checkpoint','synthesis_sha256','rtl_sources','xdc_files','ip_scripts'):
                if job.get(key)!=pm['job'].get(key):raise ValueError('Preparation parent mismatch: '+key)
            times=[line.split('\t') for line in (parent/'stage_times.tsv').read_text().splitlines()]
            if not any(row[0]=='write_opt_dcp' and row[2]=='0' for row in times):raise ValueError('Incomplete original opt checkpoint')
            inherited=['base_opt.dcp']
            if static_containment_enabled(job):
                inherited += ['static_containment.tsv','rp_footprints.tsv','containment_preflight_drc.rpt','containment_preflight_passed.txt','static_capacity.rpt','static_removed_ranges.tcldata','static_derived_ranges.tcldata']
                inherited += [rp['id']+'_route_tiles.tcldata' for rp in job['partitions']]
                if job.get('static_region_granularity') == 'clock_region':
                    inherited += ['static_region_plan.tcldata', 'rp_footprint_clock_regions.tsv']
            m['prepare_recovery']=dict(parent=str(parent),parent_manifest_sha256=sha(parent/'manifest.json'),files={})
            for name in inherited:
                shutil.copy2(parent/name,out/name);m['prepare_recovery']['files'][name]=sha(out/name)
            job['resume_opt_checkpoint']=str(out/'base_opt.dcp')
        job['prepare_worker_shells']=False
        job.setdefault('prepare_common_static_shell', True)
        extra=dict(modeled_sites=str(out/'modeled_sites.txt'),dsp_critical=int(cfg.get('DSPCritical')=='true'))
        if job.get('native_cache_capture'):
            if job.get('reuse_prepared_input_run'):raise ValueError('Native capture requires ports from the matching ordinary full export; use explicit historical migration for old runs')
            extra['native_cache_capture']=1
            extra['native_resource_inventory']=int(job.get('native_dfx_edge_policy','off')!='off')
        settings_path=settings(job,out,extra)
        save(out/'inputs/job.json',job)
        if job.get('recover_exact_preparation'):
            from recover_full_preparation import recover
            m['preparation_recovery']=measured('exact_preparation_recovery_seconds', lambda: recover(job,cfg,out))
        elif job.get('reuse_prepared_input_run'):
            update('validate_reused_inputs')
            measured('prepared_input_reuse_seconds', lambda: reuse(job, cfg, out, m))
            stage('srl_metadata_refresh', [job['vivado'], '-mode', 'batch', '-source', str(out/'inputs/dfx_refresh_srl.tcl'), '-tclargs', str(settings_path)])
            m['srl_metadata_refresh'] = verify_srl_refresh(out)
        else:
            stage('prepare',[job['vivado'],'-mode','batch','-source',str(out/'inputs/dfx_full_prepare.tcl'),'-tclargs',str(settings_path)])
        if job.get('full_edge_policy') and not job.get('recover_exact_preparation'):
            stage('full_edge_policy', [job['boundary_policy_tool'], str(out/'resources.tsv'),
                  str(out/'resource_regions.tsv'), str(out/'hard_region_sites.tsv'),
                  str(out/'hard_region_sites.policy.tsv'), str(out/'full_edge_policy_audit.tsv')])
            shutil.copy2(out/'hard_region_sites.tsv', out/'hard_region_sites.before_policy.tsv')
            (out/'hard_region_sites.policy.tsv').replace(out/'hard_region_sites.tsv')
        if job.get('fresh_rtl_required'):
            forbidden = [k for k in job if k.startswith(('resume_', 'reuse_', 'prepare_from_'))]
            if forbidden or not job.get('rtl_sources'):
                raise ValueError('Fresh RTL flow contains recovery inputs: '+str(forbidden))
            m['fresh_synthesis'] = dict(checkpoint=str(out/'base_synth.dcp'),
                sha256=sha(out/'base_synth.dcp'), reused=bool(job.get('recover_exact_preparation')),
                period_ns=job['clock_period_ns'])
        with (out/'cells.tsv').open() as f:m['primitive_counts']=dict(Counter(row['primitive'] for row in csv.DictReader(f,delimiter='\t')))
        for kind in job.get('require_fabric_types',[]):
            if not m['primitive_counts'].get(kind):raise ValueError('Fixture primitive optimized away or missing: '+kind)
        if job.get('export_equivalence_reference'):
            m['export_prefix_equivalence']=measured('export_equivalence_seconds',lambda:verify_export_prefix(job['export_equivalence_reference'],out))
        def compress_netlist():
            with zipfile.ZipFile(out/'netlist.zip','w',zipfile.ZIP_DEFLATED) as z:z.write(out/'netlist.txt','netlist')
        update('compress_amf_input')
        if not job.get('reuse_prepared_input_run') and not job.get('recover_exact_preparation'):
            measured('netlist_compression_seconds',compress_netlist)
        else:
            m['preparation_timings']['netlist_compression_reused'] = True
        cfg.update({'IncremenralCompilation':'true','DFX placement mode':'full',
                    'DFX regions file':str(out/'hard_regions.tsv'),'DFX region sites file':str(out/'hard_region_sites.tsv'),
                    'SRL control metadata file':str(out/'srl_controls.tsv'),'DSP registered outputs file':str(out/'dsp_registered_outputs.txt'),
                    'vivado extracted design information file':str(out/'netlist.zip'),'clock file':str(out/'clocks'),
                    'ClockPeriod':str(job['clock_period_ns']),'dumpDirectory':str(out/'placement'),
                    'DumpCLBPacking':'DumpCLBPacking','BoundaryReportDirectory':str(out/'reports/physical')})
        save(out/'amf_config.json',cfg)
        if job.get('native_cache_capture'):
            (out/'native_cache').mkdir()
            cfg.update({'Native static cache write':str(out/'native_cache/proposed.bin'),
                'Native static identity':job['native_static_identity'],
                'Native full input sha256':sha(out/'netlist.zip'),
                'Native source sha256':sha(binary),
                'Native full ports file':str(out/'native_ports.tsv'),
                'Native dedicated interfaces file':str(out/'dedicated_interface_pins.tsv'),
                'Native boundary pins file':str(out/'native_boundary_pins.tsv')})
            save(out/'amf_config.json',cfg)
        m['input_hashes']=measured('input_hashing_seconds',lambda:{k:sha(v) for k,v in cfg.items() if ('file' in k or k=='mergedSharedCellType2sharedCellType') and Path(v).is_file()})
        m['full_input_coverage']=dict(line.split('\t',1) for line in (out/'full_input_coverage.tsv').read_text().splitlines())
        stage('amf',[str(binary),str(out/'amf_config.json')],dict(os.environ,AMF_PROFILE_OUTPUT=str(out/'amf_profile.tsv'),AMF_GRID_UPDATE_MODE='batched'))
        if job.get('native_cache_capture'):
            audit_cfg={'Native cache action':'audit-spec','Native static cache proposal':str(out/'native_cache/proposed.bin'),
                       'Native context audit specification':str(out/'native_cache/context-spec.tsv')}
            save(out/'native_cache/audit-spec.json',audit_cfg)
            stage('native_context_spec',[str(binary),'--native-cache',str(out/'native_cache/audit-spec.json')])
        if job.get('require_srl_sharing_observed'):
            sharing = []
            for line in (out/'logs/amf.log').read_text(errors='replace').splitlines():
                if 'AMF_SRL_SHARING ' in line:
                    sharing.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', line)})
            m['srl_sharing_observed'] = sharing
            if not any(row.get('eligible_srl_pus', 0)>0 and row.get('sites_saved', 0)>0 for row in sharing):
                raise RuntimeError('SRL share4 configured but no actual shared sites observed; backend not launched')
        with (out/'cells.tsv').open() as f:cells={row['cell']:row['region'] for row in csv.DictReader(f,delimiter='\t')}
        with (out/'hard_region_sites.tsv').open() as f:owners={row['site']:row['region'] for row in csv.DictReader(f,delimiter='\t')}
        rows=placement_rows(out/'placement/DumpCLBPacking-first-0.tcl',set(cells),set(owners),set())
        for cell,target in rows.items():
            if owners[target.split('/')[0]]!=cells[cell]:raise ValueError('Full AMF region violation: '+cell)
        (out/'placement.tsv').write_text(''.join(n+'\t'+v+'\n' for n,v in sorted(rows.items())))
        m['full_placement_coverage']=dict(expected=len(cells),exported=len(rows),fraction=1,hard_region_violations=0)
        stage('backend',[job['vivado'],'-mode','batch','-source',str(out/'inputs/dfx_full_backend.tcl'),'-tclargs',str(settings_path)])
        m['acceptance']=acceptance(out,'base')
        if not m['acceptance']['implementation_verified']:raise RuntimeError('Full routing/DRC acceptance failed')
        outputs=['base_imported.dcp','base_placed.dcp','base_routed.dcp','base_full.bit']
        outputs += [r['id']+'_partial.bit' for r in job['partitions']]
        if job['prepare_common_static_shell']:
            m['common_static_shell']=record_common_static(job,out,out/'base_routed.dcp')
            outputs += common_static_artifacts(job)
        if job.get('native_cache_capture'):
            from native_cache_contract import seal_full
            m['native_static_cache']=seal_full(job,out,binary,cfg,stage,m['acceptance'],m['common_static_shell'])
            outputs += ['native_cache/sealed.bin','native_cache/contract.json']
            # Sealing adds its own stage timing; bind the final common-shell record.
            m['common_static_shell']=record_common_static(job,out,out/'base_routed.dcp')
        m['outputs']={n:sha(out/n) for n in outputs}
        m['state']='completed'
    except Exception as exc:
        m['state']='failed';m['error']=str(exc)
    finally:
        m['ended_epoch']=time.time();m['wall_seconds']=m['ended_epoch']-m['started_epoch'];update('finished')
    return 0 if m['state']=='completed' else 2


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('config','output','binary'):p.add_argument('--'+k,required=True)
    raise SystemExit(run(p.parse_args()))
