#!/usr/bin/env python3
"""Frozen synthesis-checkpoint experiment; a preflight gate precedes all P&R."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from run_dfx import sha, save
from run_dfx_multi import validate, settings, acceptance


def require_preflight(job):
    # This legacy entry point has no whole-design AMF placement step. Do not
    # allow it to silently supply native Vivado placement as an AMF reference.
    if job.get('preflight_only') is not True:
        raise ValueError('This entry point is preflight-only: whole-design AMF placement with both RP masks and the static complement is not connected. Native Vivado placement is not an eligible full reference.')


def run(config, output):
    config = Path(config).resolve()
    job = validate(json.loads(config.read_text()))
    require_preflight(job)
    checkpoint = Path(job['resume_synthesis_checkpoint'])
    if sha(checkpoint) != job['synthesis_sha256']:
        raise ValueError('Synthesis DCP hash mismatch')
    if float(job['clock_period_ns']) != 10 or int(job['vivado_threads']) != 4:
        raise ValueError('Unexpected experiment conditions')
    out = Path(output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out/'inputs').mkdir(); (out/'logs').mkdir()
    source = Path(__file__).resolve().parent
    # All helper code is frozen before dispatch. History is never overwritten.
    for f in source.iterdir():
        if f.is_file() and f.suffix in ('.tcl', '.py'):
            shutil.copy2(f, out/'inputs'/f.name)
    shutil.copy2(config, out/'inputs/job.json')
    hashes = {f.name: sha(f) for f in (out/'inputs').iterdir() if f.is_file()}
    cfg = settings(job, out)
    manifest = dict(schema='amf-dfx-multi-shell-v1', state='running', job=job,
                    started_epoch=time.time(), started=dt.datetime.now().astimezone().isoformat(),
                    launcher_pid=os.getpid(), host=os.uname().nodename,
                    source_provenance=job['source_provenance'], script_hashes=hashes,
                    input_hashes={str(checkpoint): job['synthesis_sha256']},
                    timing_scope='post-synthesis implementation; original synthesis reported separately',
                    performance_contract=job['performance_contract'], dcp_storage='server-only')
    def update(stage):
        save(out/'manifest.json', manifest)
        save(out/'status.json', dict(state=manifest['state'], stage=stage, error=manifest.get('error')))
    update('preflight_then_implementation')
    start = time.monotonic()
    try:
        cmd = [job['vivado'], '-mode', 'batch', '-source', str(out/'inputs/dfx_multi_shell.tcl'), '-tclargs', str(cfg)]
        manifest['command'] = cmd
        with (out/'logs/vivado.log').open('w') as log:
            process = subprocess.Popen(cmd, cwd=out, stdout=log, stderr=subprocess.STDOUT)
            manifest['vivado_pid'] = process.pid; update('preflight_then_implementation')
            manifest['exit_code'] = process.wait()
        if manifest['exit_code']:
            raise RuntimeError('Vivado failed; preserve this run and inspect its log')
        if not (out/'containment_preflight_passed.txt').exists():
            raise RuntimeError('Missing successful containment preflight')
        if not job.get('preflight_only', False):
            manifest['acceptance'] = acceptance(out, 'base')
            if not manifest['acceptance']['implementation_verified']:
                raise RuntimeError('Route/DRC implementation acceptance failed')
            names = ['base_placed.dcp', 'base_routed.dcp', 'base_full.bit']
            names += [r['id']+'_partial.bit' for r in job['partitions']]
            if job['prepare_worker_shells']:
                names += [r['id']+'_locked.dcp' for r in job['partitions']]
            manifest['outputs'] = {n: sha(out/n) for n in names}
        manifest['state'] = 'completed'
    except Exception as exc:
        manifest['state'] = 'failed'; manifest['error'] = str(exc)
    finally:
        manifest['wall_seconds'] = time.monotonic()-start
        manifest['ended_epoch'] = time.time()
        update('finished')
    return 0 if manifest['state'] == 'completed' else 2


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True); p.add_argument('--output', required=True)
    args = p.parse_args()
    raise SystemExit(run(args.config, args.output))
