#!/usr/bin/env python3
"""Finite Abstract Shell generation, RM backend, and full-design reassembly validation."""
import argparse
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


def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def save(p, data):
    tmp = p.with_suffix(p.suffix + '.tmp')
    tmp.write_text(json.dumps(data, indent=2) + '\n')
    tmp.replace(p)


def tcl_word(value):
    s = str(value)
    for a, b in [('\\', '\\\\'), ('"', '\\"'), ('$', '\\$'), ('[', '\\['), (']', '\\]'), ('\n', '\\n'), ('\r', '\\r')]:
        s = s.replace(a, b)
    return '"' + s + '"'


def acceptance(dest, phase):
    from summarize_face_detect_flow import timing, drc
    from summarize_full_flow import routing_complete
    if phase == 'generate':
        assert (dest / 'shell_generation_passed.txt').read_text().strip() == 'passed'
        return dict(shell_generation_passed=True, vendor_verification='inside write_abstract_shell')
    label = 'routed' if phase == 'implement' else 'assembled'
    marker = 'pr_verify_abstract_passed.txt' if phase == 'implement' else 'pr_verify_full_passed.txt'
    assert (dest / marker).read_text().strip() == 'passed'
    counts = {}
    for line in (dest / 'route_status.rpt').read_text().splitlines():
        m = re.search(r'# of\s+(.+?)\s*:\s*(\d+)', line)
        if m:
            counts[m[1].rstrip('. ')] = int(m[2])
    d = drc(dest / (label + '_drc.rpt'))
    t = timing(dest / (label + '_timing.rpt'))
    rows = list(csv.DictReader((dest / (label + '_retention.tsv')).open(), delimiter='\t'))
    exact = sum(r['requested'] == r['loc'] + '/' + r['bel'].split('.')[-1] for r in rows)
    critical = [line for line in (dest / 'vivado.log').read_text(errors='replace').splitlines()
                if line.startswith('CRITICAL WARNING:')]
    result = dict(routing_counts=counts, routing_complete=routing_complete(counts), drc=d, timing=t,
                  rm_cells=len(rows), rm_exact=exact, rm_moved=len(rows)-exact,
                  pr_verify_full_check=True, runtime_critical_warnings=critical,
                  timing_scope='active module Abstract Shell' if phase == 'implement' else 'full A1/B0 configuration')
    result['implementation_verified'] = (routing_complete(counts) and len(rows) == exact == 2956
                                        and not d['totals'].get('Error', 0)
                                        and not d['totals'].get('Critical Warning', 0))
    save(dest / 'acceptance.json', result)
    if not result['implementation_verified']:
        raise RuntimeError('Acceptance failed; inspect acceptance.json')
    return result


def run(args):
    bundle = args.bundle.resolve()
    meta = json.loads((bundle / 'bundle.json').read_text())
    for rel, desc in meta['files'].items():
        if sha(bundle / rel) != desc['sha256']:
            raise ValueError('Input hash mismatch: ' + rel)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(bundle / 'scripts', out / 'inputs')
    shutil.copy2(bundle / 'bundle.json', out / 'inputs/bundle.json')
    sys.path.insert(0, str(out / 'inputs'))
    version = subprocess.check_output([args.vivado, '-version'], text=True, stderr=subprocess.STDOUT)
    if 'vivado v2026.1' not in version or '6511674' not in version:
        raise ValueError('Unexpected Vivado version: ' + version)
    (out / 'inputs/vivado-version.txt').write_text(version)
    m = dict(schema='dfx-abstract-validation-v1', state='running', started_epoch=time.time(),
             host=socket.gethostname(), pid=os.getpid(), source=meta, phases={},
             vivado=args.vivado, vivado_version=version, amf_rerun=False,
             backend_mode='route-only', scope='A1/B0; combined A1/B1 remains separate')
    save(out / 'manifest.json', m)
    job = json.loads((bundle / 'a/job.json').read_text())
    try:
        for phase in ['generate', 'implement', 'assemble']:
            # Account for other jobs; never start the next phase under memory pressure.
            available = int(re.search(r'^MemAvailable:\s+(\d+)', Path('/proc/meminfo').read_text(), re.M)[1])
            if available < 40 * 1024 * 1024:
                raise RuntimeError('Less than 40 GiB available before ' + phase + '; preserve outputs for resume')
            dest = out / phase
            dest.mkdir(); (dest / 'reports').mkdir()
            save(out / 'status.json', dict(state='running', phase=phase))
            values = dict(job, phase=phase, output=str(dest), static_placement_audit='skip', vivado_threads=4,
                          reference_checkpoint=str(bundle / 'base_routed.dcp'),
                          control_imported=str(bundle / 'a/imported.dcp'),
                          rm_checkpoint=str(bundle / 'a/rm.dcp'),
                          generation_dir=str(out / 'generate'), implementation_dir=str(out / 'implement'),
                          abstract_checkpoint=str(out / 'generate/reports/a_abstract.dcp'),
                          placement_table=str(bundle / 'a/placement.tsv'),
                          allowed_sites=str(bundle / 'a/allowed_sites.txt'),
                          blocked_sites=str(bundle / 'a/blocked_sites.txt'), expected_cells=2956)
            config = dest / 'settings.tcl'
            config.write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in values.items())+'\n')
            start = time.time()
            cmd = ['/usr/bin/time','-v','-o',str(dest / 'process-resource.txt'),args.vivado,
                   '-mode','batch','-source',str(out / 'inputs/dfx_abstract_validation.tcl'),
                   '-tclargs',str(config)]
            item = dict(state='running', started_epoch=start, command=cmd, available_kib=available)
            m['phases'][phase] = item
            with (dest / 'launcher.log').open('w') as f:
                p = subprocess.Popen(cmd,cwd=dest,stdout=f,stderr=subprocess.STDOUT)
                item['pid'] = p.pid; save(out / 'manifest.json',m)
                item['exit_code'] = p.wait()
            item['wall_seconds'] = time.time()-start
            item['ended_epoch'] = time.time()
            if item['exit_code']:
                item['state']='failed'
                raise RuntimeError(phase+' failed; preserve failure.txt and checkpoints')
            item['acceptance'] = acceptance(dest, phase)
            required = {'generate':['a_abstract.dcp'], 'implement':['routed.dcp','rm_routed.dcp','partial.bit'],
                        'assemble':['assembled_routed.dcp','full.bit','partial.bit']}[phase]
            for name in required:
                path=dest/'reports'/name
                if not path.exists() or not path.stat().st_size:
                    raise RuntimeError('Missing output '+str(path))
            item['outputs']={f.name:dict(path=str(f),bytes=f.stat().st_size,sha256=sha(f))
                             for f in (dest/'reports').iterdir() if f.is_file()}
            item['state']='completed'; save(out/'manifest.json',m)
        m['state']='completed'
    except Exception as e:
        m['state']='failed'; m['error']=str(e)
        for item in m['phases'].values():
            if item['state']=='running':
                item['state']='failed'; item['error']=str(e)
    finally:
        m['ended_epoch']=time.time(); m['total_wall_seconds']=time.time()-m['started_epoch']
        save(out/'manifest.json',m); save(out/'status.json',dict(state=m['state'],error=m.get('error')))
    return 0 if m['state']=='completed' else 2


if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bundle',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--vivado',required=True)
    raise SystemExit(run(ap.parse_args()))
