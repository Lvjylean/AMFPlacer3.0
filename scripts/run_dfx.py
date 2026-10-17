#!/usr/bin/env python3
"""Recorded, opt-in, one-RP DFX placement in a previously locked static shell.

All paths in a job resolve relative to the job file; AMF configuration paths
resolve relative to that configuration. No output directory is reused.
"""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile
from summarize_dfx import summarize

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = Path(__file__).resolve().parent


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()


def source_provenance(binary=None):
    """Trace source archives without pretending they are Git checkouts."""
    try:
        result = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                text=True, capture_output=True, timeout=15)
        if result.returncode == 0:
            commit = result.stdout.strip()
            if not re.fullmatch(r'[0-9a-fA-F]{40}', commit):
                raise ValueError('Invalid Git source commit')
            return dict(source_commit=commit, method='git_worktree',
                        scope='worktree commit; frozen orchestration script hashes remain authoritative')
        unavailable = result.stderr.strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        unavailable = str(error)
    if binary is None:
        return dict(source_commit=None, method='script_hash_archive',
                    scope='no AMF binary used; source identified by frozen script hashes',
                    git_unavailable=unavailable)
    binary = Path(binary).resolve()
    manifest = binary.parent.parent/'manifest.json'
    build = json.loads(manifest.read_text())
    if build.get('binaries', {}).get('AMFPlacer') != sha(binary):
        raise ValueError('Frozen build provenance does not match executable')
    commit = build.get('source_commit', '')
    if not re.fullmatch(r'[0-9a-fA-F]{40}', commit):
        raise ValueError('Missing valid frozen binary source commit')
    return dict(source_commit=commit, method='validated_frozen_build_and_script_hashes',
                scope=build.get('source_commit_role', 'frozen build base commit; snapshot hashes identify dirty source'),
                build_manifest=str(manifest), build_manifest_sha256=sha(manifest),
                source_patch_sha256=build.get('source_patch_sha256'),
                source_patch_files=build.get('source_patch_files'),
                git_unavailable=unavailable)


def save(path, data):
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    tmp.replace(path)


def tcl_word(value):
    s = str(value)
    for a, b in [('\\', '\\\\'), ('"', '\\"'), ('$', '\\$'), ('[', '\\['), (']', '\\]'),
                 ('\n', '\\n'), ('\r', '\\r')]: s = s.replace(a, b)
    return '"' + s + '"'


def load_job(path):
    path = Path(path).resolve()
    job = json.loads(path.read_text())
    if job.get('IncremenralCompilation') not in (True, 'true'):
        raise ValueError('DFX entry requires explicit IncremenralCompilation=true')
    for key in ('part', 'active_partition', 'pblock', 'clock_name', 'clock_period_ns',
                'amf_config', 'static_checkpoint', 'reference_checkpoint', 'rm_checkpoint', 'vivado'):
        if key not in job: raise ValueError('Missing DFX job key: ' + key)
    if not 0 < float(job['clock_period_ns']) < 10000: raise ValueError('Invalid clock period')
    for key in ('amf_config', 'static_checkpoint', 'reference_checkpoint', 'rm_checkpoint', 'vivado'):
        job[key] = str((path.parent / job[key]).resolve())
        if not Path(job[key]).is_file(): raise ValueError('Missing input: ' + job[key])
    if not job['active_partition'] or job['active_partition'].endswith('/'):
        raise ValueError('Invalid active partition')
    job.setdefault('vivado_threads', 4)
    if job.get('static_placement_audit', 'full') not in ('full', 'skip'):
        raise ValueError('Invalid static_placement_audit mode')
    if 'resume_linked_checkpoint' in job:
        raise ValueError('Use prepare_from_run for validated linked-checkpoint recovery')
    if 'prepare_from_run' in job:
        job['prepare_from_run'] = str((path.parent / job['prepare_from_run']).resolve())
    from optimized_rm_contract import verify_optimized
    contract_path = (path.parent / job['rm_optimization_manifest']).resolve()
    proof = verify_optimized(contract_path, job)
    if proof['output'] != job['rm_checkpoint']:
        raise ValueError('Optimized checkpoint path mismatch')
    job['rm_optimization_manifest'] = str(contract_path)
    job['rm_optimization_validated'] = 1
    return job


def check_prepare_resume(job):
    """Recover export from a completed linked DCP, never from a live parent."""
    parent = Path(job['prepare_from_run'])
    old = json.loads((parent/'manifest.json').read_text())
    status = json.loads((parent/'status.json').read_text())
    if old.get('schema') != 'amf-dfx-run-v1' or old.get('state') not in ('failed', 'prepared', 'completed'):
        raise ValueError('Linked recovery requires a stopped DFX parent')
    if status.get('state') == 'running': raise ValueError('Linked parent is still running')
    for key in ('part', 'active_partition', 'pblock', 'clock_name', 'clock_period_ns', 'vivado'):
        if old['job'].get(key) != job.get(key): raise ValueError('Linked parent mismatch: '+key)
    for key in ('static_checkpoint', 'reference_checkpoint', 'rm_checkpoint', 'amf_config'):
        if sha(job[key]) != old['input_hashes'][key]: raise ValueError('Linked input hash mismatch: '+key)
    rows = [line.split('\t') for line in (parent/'stage_times.tsv').read_text().splitlines()]
    if not any(row[0] == 'write_linked_dcp' and row[2] == '0' for row in rows):
        raise ValueError('Linked checkpoint write was not completed')
    names = ['linked.dcp', 'shell_boundary.tcldata']
    if job.get('static_placement_audit', 'full') == 'full': names.append('static_snapshot.tcldata')
    files = {}
    for name in names:
        p = parent/name
        if not p.is_file() or not p.stat().st_size: raise ValueError('Missing linked recovery artifact: '+name)
        files[name] = dict(path=str(p), sha256=sha(p), bytes=p.stat().st_size)
    return dict(parent_run=str(parent), parent_manifest_sha256=sha(parent/'manifest.json'), files=files,
                inherited_stage_seconds={row[0]:float(row[1]) for row in rows if row[2]=='0'},
                link_and_opt_rerun=False,
                timing_note='Current wall excludes inherited link/opt; checkpoint reread/copy and interrupted parent overhead remain separate')


def check_complete_prepare_resume(job):
    """Reuse a successful immutable preparation of a stopped failed worker."""
    parent = Path(job['reuse_prepared_input_from_run'])
    linked_job = dict(job, prepare_from_run=str(parent))
    recovery = check_prepare_resume(linked_job)
    old = json.loads((parent/'manifest.json').read_text())
    if not any(s['name']=='prepare' and s['exit_code']==0 for s in old['stages']):
        raise ValueError('Complete preparation recovery requires successful prepare')
    if old.get('amf_input_scope', {}).get('schema') != 'rm-boundary-cone-v1':
        raise ValueError('Unexpected prepared interface scope')
    names = ('allowed_sites.txt', 'blocked_sites.txt', 'resource_scope.tsv',
             'cells.tsv', 'static_placement.tsv', 'interface_context.tsv',
             'srl_controls.tsv', 'netlist.txt', 'dsp_registered_outputs.txt',
             'dsp_certification.tsv', 'clocks', 'input_scope.tsv', 'linked_boundary.tcldata')
    for name in names:
        f = parent/name
        if not f.is_file() or (not f.stat().st_size and name not in ('clocks',)):
            raise ValueError('Missing complete preparation artifact: '+name)
        recovery['files'][name] = dict(path=str(f), sha256=sha(f), bytes=f.stat().st_size)
    scope = dict(line.split('\t', 1) for line in (parent/'input_scope.tsv').read_text().splitlines())
    if scope != old['amf_input_scope']: raise ValueError('Prepared scope manifest mismatch')
    with (parent/'cells.tsv').open() as f:
        active = list(csv.DictReader(f, delimiter='\t'))
    if len(active) != int(scope['active_cells']) or any(r['active']!='1' or not r['cell'].startswith(job['active_partition']+'/') for r in active):
        raise ValueError('Prepared active RM inventory mismatch')
    recovery.update(complete_preparation_reused=True, prepare_rerun=False,
                    inherited_prepare_process_seconds=next(s['seconds'] for s in old['stages'] if s['name']=='prepare'),
                    parent_input_scope=scope, parent_device_sha256=old['device_sha256'],
                    timing_note='No Vivado prepare/export rerun. Original preparation and failed AMF remain inherited overhead, separate from current wall.')
    return recovery


def amf_configuration(job):
    path = Path(job['amf_config'])
    cfg = json.loads(path.read_text())
    # Do not carry GETRF-specific netlist/control/cluster caches into another RM.
    forbidden = ('fixed units file', 'unpredictable macro file', 'designCluster file', 'designCluster',
                 'DSP registered outputs file', 'SRL control metadata file')
    for key in forbidden:
        if cfg.get(key): raise ValueError('DFX requires a case-neutral AMF template; remove ' + key)
    if any(k.startswith('external floorplan') for k in cfg):
        raise ValueError('Initialization-only external floorplan is not a hard DFX Pblock')
    for key, value in list(cfg.items()):
        if 'file' in key or key == 'mergedSharedCellType2sharedCellType':
            if key in ('vivado extracted design information file', 'clock file'): continue
            cfg[key] = str((path.parent / value).resolve())
            if not Path(cfg[key]).is_file(): raise ValueError('Missing AMF model: ' + cfg[key])
    if cfg.get('ProPrimitiveTiming') != 'false': raise ValueError('DFX work baseline is ordinary Pro')
    return cfg


def placement_rows(export, cells, allowed, blocked):
    """Parse only AMF place_cell payloads as Tcl DATA, never source the export."""
    text = Path(export).read_text()
    if not text.startswith('error {DFX placement data:'):
        raise ValueError('Not a protected DFX export')
    raw = '\n'.join(re.findall(r'\[catch \{place_cell \{(.*?)\}\}\]', text, re.S))
    if not raw.strip(): raise ValueError('Empty AMF placement export')
    parser = r'''
set raw [read stdin]
if {[llength $raw] % 2} {error "Odd placement payload"}
foreach {cell target} $raw {puts "$cell\t$target"}
'''
    # tclsh interprets only a constant parser. AMF names remain Tcl list data.
    import tempfile
    with tempfile.NamedTemporaryFile(mode='w', suffix='.tcl') as f:
        f.write(parser); f.flush()
        parsed = subprocess.check_output(['tclsh', f.name], input=raw, text=True)
    rows = {}
    for line in parsed.splitlines():
        name, target = line.split('\t')
        if name in rows or name not in cells: raise ValueError('Unexpected/duplicate exported cell: ' + name)
        site = target.split('/')[0]
        if site not in allowed or site in blocked: raise ValueError('AMF placement outside available RP sites: ' + line)
        rows[name] = target
    if set(rows) != set(cells): raise ValueError('Incomplete RM export: missing ' + repr(sorted(set(cells)-set(rows))))
    return rows


def run(args):
    job = load_job(args.config)
    job['backend_place_mode'] = 'amf-import-only'
    cfg = amf_configuration(job)
    if 'reuse_prepared_input_from_run' in job and 'prepare_from_run' in job:
        raise ValueError('Choose one preparation recovery mode')
    recovery = (check_complete_prepare_resume(job) if 'reuse_prepared_input_from_run' in job
                else check_prepare_resume(job) if 'prepare_from_run' in job else None)
    provenance = source_provenance(args.binary)
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'logs').mkdir(); (out / 'placement').mkdir(); (out / 'inputs').mkdir()
    (out / 'reports' / 'physical').mkdir(parents=True)
    started = time.monotonic()
    manifest = dict(schema='amf-dfx-run-v1', started=dt.datetime.now().astimezone().isoformat(),
                    job=job, stages=[], state='running', dcp_storage='server-only',
                    scope='active RP plus fixed interface timing cones; static routing remains in Vivado',
                    amf_placement_scope='dynamic_only_fixed_static_context',
                    source_commit=provenance['source_commit'], source_provenance=provenance,
                    input_hashes={k: sha(job[k]) for k in ('static_checkpoint','reference_checkpoint','rm_checkpoint','amf_config')})
    if recovery: manifest['prepare_recovery'] = recovery
    manifest['rm_optimization_contract_sha256'] = sha(job['rm_optimization_manifest'])
    for name in ('dfx_prepare.tcl', 'dfx_input_scope.tcl', 'dfx_srl_metadata.tcl', 'dfx_backend.tcl', 'dfx_import.tcl', 'dfx_common.tcl', 'dfx_boundary_compatibility.tcl', 'dfx_site_import.tcl', 'run_dfx.py', 'summarize_dfx.py',
                 'summarize_full_flow.py', 'summarize_face_detect_flow.py', 'optimized_rm_contract.py', 'dfx_rm_opt.tcl'):
        shutil.copy2(SCRIPT / name, out / 'inputs' / name)
    manifest['script_hashes'] = {p.name: sha(p) for p in (out/'inputs').iterdir()}
    save(out / 'manifest.json', manifest)

    def stage(name, command, cwd=None, env=None):
        save(out/'status.json', dict(state='running', stage=name))
        t = time.monotonic(); epoch_start = time.time()
        with (out/'logs'/f'{name}.log').open('w') as log:
            result = subprocess.run(command, cwd=cwd or out, env=env, stdout=log, stderr=subprocess.STDOUT)
        manifest['stages'].append(dict(name=name, command=command, seconds=time.monotonic()-t, started_epoch=epoch_start, ended_epoch=time.time(), exit_code=result.returncode))
        save(out/'manifest.json', manifest)
        if result.returncode: raise RuntimeError(f'{name} failed ({result.returncode}); see {out}/logs/{name}.log')

    try:
        # Restrict allowlists to actual modeled sites (including unrepresented
        # RAMB36 aliases would otherwise let illegal anchors through).
        device = Path(cfg['vivado extracted device information file'])
        with zipfile.ZipFile(device) as z:
            names = [n for n in z.namelist() if not n.endswith('/')]
            if len(names) != 1: raise ValueError('Ambiguous device archive')
            sites = re.findall(r'^site=> (\S+)', z.read(names[0]).decode(), re.M)
        (out/'modeled_sites.txt').write_text('\n'.join(sorted(set(sites)))+'\n')
        manifest['device_sha256'] = sha(device)
        if recovery and recovery.get('complete_preparation_reused') and manifest['device_sha256'] != recovery['parent_device_sha256']:
            raise ValueError('Prepared device model changed')
        settings = dict(job, output=str(out), modeled_sites=str(out/'modeled_sites.txt'),
                        dsp_critical=int(cfg.get('DSPCritical', 'false') == 'true'))
        if recovery:
            copy_start = time.monotonic(); copy_epoch = time.time()
            for name, info in recovery['files'].items():
                shutil.copy2(info['path'], out/name)
                if sha(out/name) != info['sha256']: raise ValueError('Recovery copy hash mismatch: '+name)
            manifest['prepare_recovery']['copy_seconds'] = time.monotonic()-copy_start
            if recovery.get('complete_preparation_reused'):
                manifest['stages'].append(dict(name='prepare_restore', command=['copy_sha256_verified_prepared_inputs', recovery['parent_run']],
                                              seconds=manifest['prepare_recovery']['copy_seconds'], started_epoch=copy_epoch,
                                              ended_epoch=time.time(), exit_code=0))
            settings['resume_linked_checkpoint'] = str(out/'linked.dcp')
            save(out/'manifest.json', manifest)
        settings_path = out/'inputs/settings.tcl'
        settings_path.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in settings.items())+'\n')
        if not (recovery and recovery.get('complete_preparation_reused')):
            stage('prepare', [job['vivado'], '-mode', 'batch', '-source', str(out/'inputs/dfx_prepare.tcl'), '-tclargs', str(settings_path)])
        manifest['amf_input_scope'] = dict(line.split('\t', 1) for line in (out/'input_scope.tsv').read_text().splitlines())
        save(out/'manifest.json', manifest)
        if args.prepare_only:
            manifest['state'] = 'prepared'
            return out
        binary = Path(args.binary).resolve()
        capability = json.loads(subprocess.check_output([str(binary), '--capabilities'], text=True))
        if capability.get('dfx_incremental_schema') != 1: raise ValueError('Binary does not implement DFX constraints')
        manifest['binary'] = str(binary); manifest['binary_sha256'] = sha(binary)
        build_manifest = binary.parent.parent/'manifest.json'
        if build_manifest.is_file():
            manifest['build'] = json.loads(build_manifest.read_text())
            if manifest['build'].get('binaries', {}).get('AMFPlacer') != manifest['binary_sha256']:
                raise ValueError('Build manifest does not match executable')
        manifest['model_input_hashes'] = {k:sha(v) for k,v in cfg.items() if ('file' in k or k=='mergedSharedCellType2sharedCellType') and Path(v).is_file()}
        with zipfile.ZipFile(out/'netlist.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(out/'netlist.txt', 'netlist')
        cfg.update({'IncremenralCompilation':'true', 'DFX input scope':'rm-boundary-cone-v1', 'DFX active partition':job['active_partition'],
                    'DFX allowed sites file':str(out/'allowed_sites.txt'), 'DFX blocked sites file':str(out/'blocked_sites.txt'),
                    'DFX static placement file':str(out/'static_placement.tsv'),
                    'SRL control metadata file':str(out/'srl_controls.tsv'),
                    'DSP registered outputs file':str(out/'dsp_registered_outputs.txt'),
                    'vivado extracted design information file':str(out/'netlist.zip'), 'clock file':str(out/'clocks'),
                    'ClockPeriod':str(job['clock_period_ns']), 'dumpDirectory':str(out/'placement'),
                    'DumpCLBPacking':'DumpCLBPacking', 'BoundaryReportDirectory':str(out/'reports/physical')})
        save(out/'amf_config.json', cfg)
        environment = dict(os.environ, AMF_PROFILE_OUTPUT=str(out/'amf_profile.tsv'), AMF_GRID_UPDATE_MODE='batched')
        stage('amf', [str(binary), str(out/'amf_config.json')], cwd=out/'placement', env=environment)
        with (out/'cells.tsv').open() as f:
            active = {r['cell'] for r in csv.DictReader(f, delimiter='\t') if r['active']=='1'}
        rows = placement_rows(out/'placement/DumpCLBPacking-first-0.tcl', active,
                              set((out/'allowed_sites.txt').read_text().splitlines()),
                              set((out/'blocked_sites.txt').read_text().splitlines()))
        (out/'placement.tsv').write_text(''.join(n+'\t'+v+'\n' for n,v in sorted(rows.items())))
        manifest['active_placement_coverage'] = dict(expected=len(active), exported=len(rows), fraction=1.0)
        stage('backend', [job['vivado'], '-mode', 'batch', '-source', str(out/'inputs/dfx_backend.tcl'), '-tclargs', str(settings_path)])
        required = ['imported.dcp','rm_amf_placed.dcp','import_retention.tsv']
        for name in required:
            if not (out/name).is_file() or not (out/name).stat().st_size:
                raise RuntimeError('Missing AMF module artifact: '+name)
        with (out/'import_retention.tsv').open() as f:
            audit=list(csv.DictReader(f,delimiter='\t'))
        actual={}
        for row in audit:
            target=row['loc']
            if '/' in row['requested']:target+='/'+row['bel'].split('.')[-1]
            if row['cell'] in actual or target!=rows.get(row['cell']):
                raise RuntimeError('AMF import changed or duplicated a cell')
            actual[row['cell']]=target
        if actual!=rows:raise RuntimeError('Incomplete AMF import audit')
        manifest['outputs'] = {name: dict(path=str(out/name), sha256=sha(out/name), bytes=(out/name).stat().st_size) for name in required}
        manifest['amf_import_acceptance'] = dict(amf_loc_bel_exact=True, cells=len(rows),
            vivado_placement_executed=False, routing_executed=False,
            final_implementation_verified=False, joint_backend_acceptance_required=True)
        manifest['state'] = 'amf_placed'
        return out
    except Exception as error:
        manifest['state'] = 'failed'; manifest['error'] = str(error)
        raise
    finally:
        manifest['total_wall_seconds'] = time.monotonic()-started
        save(out/'manifest.json', manifest)
        save(out/'status.json', dict(state=manifest['state'], error=manifest.get('error')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--binary')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if not args.prepare_only and not args.binary: parser.error('--binary required for AMF execution')
    print(run(args))


if __name__ == '__main__': main()
