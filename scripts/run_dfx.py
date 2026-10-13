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
    return job


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
    cfg = amf_configuration(job)
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'logs').mkdir(); (out / 'placement').mkdir(); (out / 'inputs').mkdir()
    (out / 'reports' / 'physical').mkdir(parents=True)
    started = time.monotonic()
    manifest = dict(schema='amf-dfx-run-v1', started=dt.datetime.now().astimezone().isoformat(),
                    job=job, stages=[], state='running', dcp_storage='server-only',
                    scope='one active fabric RP; static fabric timing context; static routing owned by Vivado',
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    input_hashes={k: sha(job[k]) for k in ('static_checkpoint','reference_checkpoint','rm_checkpoint','amf_config')})
    for name in ('dfx_prepare.tcl', 'dfx_backend.tcl', 'dfx_common.tcl', 'run_dfx.py', 'summarize_dfx.py',
                 'summarize_full_flow.py', 'summarize_face_detect_flow.py'):
        shutil.copy2(SCRIPT / name, out / 'inputs' / name)
    manifest['script_hashes'] = {p.name: sha(p) for p in (out/'inputs').iterdir()}
    save(out / 'manifest.json', manifest)

    def stage(name, command, cwd=None, env=None):
        save(out/'status.json', dict(state='running', stage=name))
        t = time.monotonic()
        with (out/'logs'/f'{name}.log').open('w') as log:
            result = subprocess.run(command, cwd=cwd or out, env=env, stdout=log, stderr=subprocess.STDOUT)
        manifest['stages'].append(dict(name=name, command=command, seconds=time.monotonic()-t, exit_code=result.returncode))
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
        settings = dict(job, output=str(out), modeled_sites=str(out/'modeled_sites.txt'))
        settings_path = out/'inputs/settings.tcl'
        settings_path.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in settings.items())+'\n')
        stage('prepare', [job['vivado'], '-mode', 'batch', '-source', str(out/'inputs/dfx_prepare.tcl'), '-tclargs', str(settings_path)])
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
        cfg.update({'IncremenralCompilation':'true', 'DFX active partition':job['active_partition'],
                    'DFX allowed sites file':str(out/'allowed_sites.txt'), 'DFX blocked sites file':str(out/'blocked_sites.txt'),
                    'DFX static placement file':str(out/'static_placement.tsv'),
                    'SRL control metadata file':str(out/'srl_controls.tsv'),
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
        required = ['imported.dcp','placed.dcp','routed.dcp','pr_verify_passed.txt','full.bit','partial.bit']
        for name in required:
            if not (out/name).is_file() or (out/name).stat().st_size == 0: raise RuntimeError('Missing DFX artifact: '+name)
        manifest['outputs'] = {name: dict(path=str(out/name), sha256=sha(out/name), bytes=(out/name).stat().st_size) for name in required}
        manifest['acceptance'] = summarize(out)
        save(out/'acceptance.json', manifest['acceptance'])
        if not manifest['acceptance']['implementation_verified']:
            raise RuntimeError('DFX implementation acceptance failed; see acceptance.json')
        manifest['state'] = 'completed'
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
