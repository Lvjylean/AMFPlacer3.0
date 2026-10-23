#!/usr/bin/env python3
"""Run route-only/Quick on identical frozen imported DCPs. No AMF rerun or retries."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import time


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def save(path, data):
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(data, indent=2) + '\n')
    tmp.replace(path)


def tcl_word(value):
    s = str(value)
    for a, b in [('\\', '\\\\'), ('"', '\\"'), ('$', '\\$'), ('[', '\\['), (']', '\\]'), ('\n', '\\n'), ('\r', '\\r')]:
        s = s.replace(a, b)
    return '"' + s + '"'


def summarize(out):
    from summarize_face_detect_flow import timing, drc
    from summarize_full_flow import routing_complete
    counts = {}
    for line in (out / 'route_status.rpt').read_text().splitlines():
        m = re.search(r'# of\s+(.+?)\s*:\s*(\d+)', line)
        if m:
            counts[m[1].rstrip('. ')] = int(m[2])
    d = drc(out / 'routed_drc.rpt')
    observed = dict((k, int(v)) for k, v in (l.split('\t') for l in (out / 'routed_drc_counts.tsv').read_text().splitlines()))
    assert observed['errors'] == d['totals'].get('Error', 0)
    assert observed['critical_warnings'] == d['totals'].get('Critical Warning', 0)
    rows = list(csv.DictReader((out / 'routed_retention.tsv').open(), delimiter='\t'))
    exact = sum(r['requested'] == r['loc'] + '/' + r['bel'].split('.')[-1] for r in rows)
    boundary = (out / 'boundary_checks.tsv').read_text().splitlines()
    pp = bool(boundary) and all(l.endswith('\tpassed') for l in boundary)
    pr = (out / 'pr_verify_passed.txt').read_text().strip() == 'full_check_passed'
    tm = timing(out / 'routed_timing.rpt')
    stages = {n: dict(seconds=float(t), exit_code=int(c)) for n, t, c in
              (l.split('\t') for l in (out / 'stage_times.tsv').read_text().splitlines())}
    return dict(routing_counts=counts, routing_complete=routing_complete(counts), drc=d,
                timing=tm, rm_requested=len(rows), rm_exact_matches=exact, rm_moved=len(rows)-exact,
                boundary_unchanged=pp, pr_verify_full_check=pr, stages=stages,
                implementation_verified=routing_complete(counts) and not any(observed.values()) and pr and pp,
                amf_placement_preserved=bool(rows) and exact == len(rows),
                timing_met=all(tm[k] >= 0 for k in ('wns_ns', 'whs_ns', 'wpws_ns')),
                placement_route_seconds=stages.get('vivado_place', {'seconds': 0})['seconds'] + stages['vivado_route']['seconds'])


def run(args):
    bundle = args.bundle.resolve()
    meta = json.loads((bundle / 'bundle.json').read_text())
    for rel, desc in meta['files'].items():
        if sha(bundle / rel) != desc['sha256']:
            raise ValueError('Input hash mismatch: ' + rel)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'inputs').mkdir()
    for p in (bundle / 'scripts').iterdir():
        if p.is_file():
            shutil.copy2(p, out / 'inputs' / p.name)
    sys.path.insert(0, str(out / 'inputs'))
    shutil.copy2(bundle / 'bundle.json', out / 'inputs' / 'bundle.json')
    version = subprocess.check_output([args.vivado, '-version'], text=True, stderr=subprocess.STDOUT)
    if 'vivado v2026.1' not in version or '6511674' not in version:
        raise ValueError('Vivado build mismatch: ' + version)
    (out / 'inputs' / 'vivado-version.txt').write_text(version)
    record = dict(schema='dfx-place-mode-probe-v1', state='running', started_epoch=time.time(),
                  host=socket.gethostname(), pid=os.getpid(), max_parallel=args.jobs,
                  vivado=args.vivado, vivado_version=version, source=meta, results={},
                  amf_rerun=False, parent_artifacts_modified=False, dcp_storage='server-only')
    save(out / 'manifest.json', record)
    save(out / 'status.json', dict(state='running', stage='backend_variants'))

    def worker(arm, mode):
        name = arm + '-' + mode
        dest = out / name
        dest.mkdir(); (dest / 'reports').mkdir(); (dest / 'inputs').mkdir()
        start = time.time()
        m = dict(state='running', mode=mode, arm=arm, started_epoch=start, host=record['host'],
                 parent=meta['arms'][arm]['parent'], input_sha256=meta['files'][arm+'/imported.dcp']['sha256'])
        save(dest / 'manifest.json', m)
        try:
            job = json.loads((bundle / arm / 'job.json').read_text())
            values = dict(job, mode=mode, output=str(dest), vivado_threads=4, static_placement_audit='skip',
                          imported_checkpoint=str(bundle / arm / 'imported.dcp'),
                          reference_checkpoint=str(bundle / 'base_routed.dcp'),
                          placement_table=str(bundle / arm / 'placement.tsv'),
                          allowed_sites=str(bundle / arm / 'allowed_sites.txt'),
                          blocked_sites=str(bundle / arm / 'blocked_sites.txt'), expected_cells=2956)
            config = dest / 'inputs' / 'settings.tcl'
            config.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in values.items())+'\n')
            # Per-job copies keep the source location and hashes self-contained.
            for p in (out / 'inputs').glob('*.tcl'):
                shutil.copy2(p, dest / 'inputs' / p.name)
            m['script_hashes'] = {p.name: sha(p) for p in (dest / 'inputs').iterdir()}
            free = subprocess.check_output(['free', '-m'], text=True)
            (dest / 'resources-before.txt').write_text(free)
            cmd = ['/usr/bin/time', '-v', '-o', str(dest / 'process-resource.txt'), args.vivado,
                   '-mode', 'batch', '-source', str(dest / 'inputs' / 'dfx_place_mode_probe.tcl'), '-tclargs', str(config)]
            m['command'] = cmd
            with (dest / 'launcher.log').open('w') as log:
                p = subprocess.Popen(cmd, cwd=dest, stdout=log, stderr=subprocess.STDOUT)
                m['pid'] = p.pid; save(dest / 'manifest.json', m)
                m['exit_code'] = p.wait()
            if m['exit_code']:
                raise RuntimeError('Vivado failed; preserved launcher.log/failure.txt')
            m['acceptance'] = summarize(dest)
            save(dest / 'acceptance.json', m['acceptance'])
            for n in ['routed.dcp','rm_routed.dcp','full.bit','partial.bit']:
                p = dest / 'reports' / n
                if not p.is_file() or not p.stat().st_size:
                    raise RuntimeError('Missing output ' + n)
            m['outputs'] = {p.name: dict(path=str(p), bytes=p.stat().st_size, sha256=sha(p))
                            for p in (dest / 'reports').iterdir() if p.is_file()}
            m['state'] = 'completed' if m['acceptance']['implementation_verified'] else 'validation_failed'
        except Exception as e:
            m['state'] = 'failed'; m['error'] = str(e)
        finally:
            m['ended_epoch'] = time.time(); m['total_wall_seconds'] = time.time() - start
            save(dest / 'manifest.json', m); save(dest / 'status.json', dict(state=m['state'], error=m.get('error')))
        return name, dict(state=m['state'], path=str(dest), error=m.get('error'), acceptance=m.get('acceptance'))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(worker, arm, mode) for arm in args.arms for mode in ['route-only', 'quick']]
        for f in concurrent.futures.as_completed(futures):
            name, result = f.result(); record['results'][name] = result; save(out / 'manifest.json', record)
    record['state'] = 'completed' if all(x['state'] == 'completed' for x in record['results'].values()) else 'completed_with_failures'
    record['ended_epoch'] = time.time(); record['total_wall_seconds'] = time.time() - record['started_epoch']
    save(out / 'manifest.json', record); save(out / 'status.json', dict(state=record['state']))
    return 0


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bundle', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--vivado', required=True); ap.add_argument('--arms', nargs='+', choices=['a','b'], required=True)
    ap.add_argument('--jobs', type=int, choices=[1,2], required=True)
    raise SystemExit(run(ap.parse_args()))
