#!/usr/bin/env python3
"""OOC export -> native cache/placement -> pure coordinate import; no static query."""
import argparse,csv,json,os,shutil,time,subprocess
from pathlib import Path
from parallel_runtime import command,verify_hashes
from run_dfx import amf_configuration,save,sha,source_provenance,tcl_word,placement_rows
from optimized_rm_contract import verify_optimized
from native_cache_contract import verify_cache, verify_export_scripts, MODEL_KEYS

def _run(config,output,binary,started_epoch):
    job=json.loads(Path(config).read_text());out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    frozen=out/'inputs';frozen.mkdir();placement=out/'placement';placement.mkdir()
    (out/'reports/physical').mkdir(parents=True)
    for p in Path(__file__).parent.glob('*.tcl'):shutil.copy2(p,frozen/p.name)
    native=job['native_static_cache'];cache=Path(native['path']);contract=Path(native['contract'])
    expected={str(cache):native['sha256'],str(contract):native['contract_sha256']}
    cfg=amf_configuration(job);c=verify_cache(native,job,cfg)
    if job.get('native_dfx_edge_policy','off')!='off':
        capability=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True))
        if capability.get('native_dfx_edge_policy_schema')!=1:
            raise ValueError('Binary cannot enforce the requested native edge policy')
    if job.get('native_pin_access_policy','off')!='off':
        capability=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True))
        if capability.get('native_pin_cache_packing_schema')!=1:
            raise ValueError('Binary cannot enforce cached pin-access masks')
    optimization=verify_optimized(job['rm_optimization_manifest'],job)
    if Path(optimization['output']).resolve()!=Path(job['rm_checkpoint']).resolve():raise ValueError('RM checkpoint is not the certified OOC opt output')
    cfg.update({'ClockPeriod':str(job['clock_period_ns']),
        'Native static cache read':str(cache),'Native static identity':c['identity'],
        'Native static shell sha256':c['static_shell_sha256'],'Native active module':job['module_id'],
        'Native DFX edge policy':job.get('native_dfx_edge_policy','off'),
        'Native pin access policy':job.get('native_pin_access_policy','off'),
        'Native module netlist':str(out/'ooc/netlist.txt'),'Native module ports file':str(out/'ooc/ports.tsv'),
        'Native workspace':str(out/'native'),'SRL control metadata file':str(out/'ooc/srl_controls.tsv'),
        'DSP registered outputs file':str(out/'ooc/dsp_registered_outputs.txt'),
        'IncremenralCompilation':'true','dumpDirectory':str(placement),'DumpCLBPacking':'DumpCLBPacking',
        'BoundaryReportDirectory':str(out/'reports/physical')})
    if cfg['device']!=c['device']:raise ValueError('Native device mismatch')
    m=dict(schema='amf-native-module-v1',state='running',started_epoch=started_epoch,job=job,stages=[],outputs={},
           preflight_seconds=time.time()-started_epoch,
           binary_sha256=sha(binary),source_provenance=source_provenance(binary),
           native_cache_contract=c,static_dcp_opened=False,place_design_executed=False,
           checkpoint_semantics='standalone OOC RM only; imported.dcp is an alias of rm_amf_placed.dcp')
    m['optimization']=optimization
    expected.update({str(Path(binary)):sha(binary),job['rm_checkpoint']:sha(job['rm_checkpoint'])})
    expected.update({cfg[k]:sha(cfg[k]) for k in MODEL_KEYS if k in cfg})
    expected[job['static_checkpoint']]=c['static_shell_sha256']
    expected[job['rm_optimization_manifest']]=optimization['manifest_sha256']
    m['script_hashes']={p.name:sha(p) for p in frozen.iterdir() if p.is_file()}
    m['input_hashes']=expected;save(out/'manifest.json',m)
    def stage(name,script,settings):
        p=frozen/(name+'-settings.tcl');p.write_text(''.join('set dfx('+k+') '+tcl_word(v)+'\n' for k,v in settings.items()))
        result=command([job['vivado'],'-mode','batch','-source',str(frozen/script),'-tclargs',str(p)],out/(name+'-exec'))
        m['stages'].append(dict(name=name,**result));save(out/'manifest.json',m)
    settings=dict(output=str(out/'ooc'),rm_checkpoint=job['rm_checkpoint'],module_id=job['module_id'],
                  clock_name=job['clock_name'],clock_period_ns=job['clock_period_ns'],vivado_threads=job['vivado_threads'])
    try:
        (out/'ooc').mkdir()
        if job.get('reuse_native_export'):
            begin=time.monotonic();recovery=job['reuse_native_export'];parent=Path(recovery['module'])
            pm=json.loads((parent/'manifest.json').read_text())
            if pm['state']!='failed':raise ValueError('OOC export recovery parent must be stopped and failed')
            for key in ('module_id','active_partition','part','clock_name','clock_period_ns','vivado_threads','vivado','native_static_cache'):
                if pm['job'][key]!=job[key]:raise ValueError('OOC export recovery identity mismatch: '+key)
            if sha(pm['job']['rm_checkpoint'])!=sha(job['rm_checkpoint']):raise ValueError('OOC export has a different optimized netlist')
            stages=[s for s in pm['stages'] if s['name']=='ooc_export' and s.get('exit_code')==0]
            if len(stages)!=1:raise ValueError('Missing successful OOC export producer')
            # Recovery depends on the exporter and all of its sourced files.
            # A corrected coordinate importer does not invalidate raw OOC data.
            m['export_recovery_script_hashes']=verify_export_scripts(pm['script_hashes'],m['script_hashes'])
            names=('netlist.txt','ports.tsv','srl_controls.tsv','dsp_registered_outputs.txt')
            required={str(parent/'ooc'/name) for name in names}|{str(parent/'manifest.json')}
            if set(recovery['hashes'])!=required:raise ValueError('Incomplete OOC export recovery hash coverage')
            verify_hashes(pm['input_hashes']);verify_hashes(recovery['hashes'])
            for name in names:shutil.copy2(parent/'ooc'/name,out/'ooc'/name)
            for name in names:
                if sha(out/'ooc'/name)!=recovery['hashes'][str(parent/'ooc'/name)]:raise ValueError('OOC recovery copy differs')
            expected.update(recovery['hashes'])
            m.update(inherited_ooc_export=dict(parent=str(parent),stage=stages[0],hashes=recovery['hashes']),
                     ooc_recovery_copy_and_validation_seconds=time.monotonic()-begin)
            save(out/'manifest.json',m)
        else:stage('ooc_export','dfx_native_rm_export.tcl',settings)
        save(out/'amf_config.json',cfg)
        env=dict(os.environ,AMF_PROFILE_OUTPUT=str(out/'amf_profile.tsv'),AMF_GRID_UPDATE_MODE='batched')
        result=command([binary,str(out/'amf_config.json')],out/'amf-exec',env=env)
        m['stages'].append(dict(name='amf',**result));save(out/'manifest.json',m)
        for name in ('cells.tsv','allowed_sites.txt','blocked_sites.txt','static_placement.tsv','input_scope.tsv','interface_endpoints.tsv','interface_buffers.tsv'):
            shutil.copy2(out/'native'/name,out/name)
        with (out/'cells.tsv').open() as f:active={r['cell'] for r in csv.DictReader(f,delimiter='\t') if r['active']=='1'}
        rows=placement_rows(placement/'DumpCLBPacking-first-0.tcl',active,set((out/'allowed_sites.txt').read_text().splitlines()),set((out/'blocked_sites.txt').read_text().splitlines()))
        (out/'placement.tsv').write_text(''.join(n+'\t'+v+'\n' for n,v in sorted(rows.items())))
        settings.update(output=str(out),active_partition=job['active_partition'])
        if job.get('clock_handoff_mode') == 'inherit_static_primary':
            settings.update(clock_handoff_mode=job['clock_handoff_mode'],rm_clock_port=job['rm_clock_port'])
        stage('coordinate_import','dfx_native_rm_import.tcl',settings)
        os.link(out/'rm_amf_placed.dcp',out/'imported.dcp')
        for name in ('rm_amf_placed.dcp','imported.dcp'):
            p=out/name;m['outputs'][name]=dict(sha256=sha(p),path=str(p),bytes=p.stat().st_size)
        verify_hashes(expected)
        with (out/'interface_buffers.tsv').open() as f:buffers=list(csv.DictReader(f,delimiter='\t'))
        m.update(state='amf_placed',amf_import_acceptance=dict(amf_loc_bel_exact=True,cells=len(rows),interface_buffers=len(buffers)),
                 interface_buffer_plan=dict(sha256=sha(out/'interface_buffers.tsv'),rows=buffers,policy='AMF-generated LUT1 INIT=2 shared data-output normalization'))
    except Exception as e:m.update(state='failed',error=repr(e));raise
    finally:
        m['finished_epoch']=time.time();m['total_wall_seconds']=m['finished_epoch']-m['started_epoch'];save(out/'manifest.json',m)

def run(config,output,binary):
    out=Path(output).resolve()
    if out.exists():raise ValueError('Refusing to overwrite an existing native worker')
    start=time.time()
    try:return _run(config,output,binary,start)
    except Exception as e:
        if out.is_dir():
            p=out/'manifest.json';m=json.loads(p.read_text()) if p.exists() else {'schema':'amf-native-module-v1','started_epoch':start,'stages':[]}
            m.update(state='failed',error=repr(e),finished_epoch=time.time())
            m['total_wall_seconds']=m['finished_epoch']-m['started_epoch'];save(p,m)
        raise
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True);p.add_argument('--binary',required=True)
    a=p.parse_args();run(a.config,a.output,a.binary)
