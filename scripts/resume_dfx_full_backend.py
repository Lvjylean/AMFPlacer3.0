#!/usr/bin/env python3
"""Resume a failed full DFX backend in a fresh directory; never rerun AMF."""
from common_static_contract import record as record_common_static, artifact_names as common_static_artifacts
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import time
from run_dfx import save, sha
from run_dfx_multi import settings, acceptance


def run(a):
    parent = Path(a.parent).resolve()
    pm = json.loads((parent / 'manifest.json').read_text())
    if pm.get('state') != 'failed' or pm.get('amf_placement_scope') != 'whole_design':
        raise ValueError('Recovery requires an inactive, failed whole-design run')
    amf = [s for s in pm['stages'] if s['name'] == 'amf']
    if len(amf) != 1 or amf[0]['exit_code'] != 0:
        raise ValueError('No successful AMF stage to reuse')
    coverage = pm['full_placement_coverage']
    if coverage['fraction'] != 1 or coverage['hard_region_violations'] != 0:
        raise ValueError('Parent placement is incomplete or violates hard regions')
    if sha(pm['binary']) != pm['binary_sha256']:
        raise ValueError('Parent binary hash changed')
    old_opt_sha = (pm.get('prepare_recovery', {}).get('files', {}).get('base_opt.dcp')
                   or pm.get('recovery', {}).get('files', {}).get('base_opt.dcp'))
    if old_opt_sha and sha(parent / 'base_opt.dcp') != old_opt_sha:
        raise ValueError('Parent opt DCP hash changed')
    labels = dict(imported='write_imported', placed='write_placed_dcp', routed='write_routed_dcp')
    phase_path = parent / 'stage_times.tsv'
    phases = [line.split('\t') for line in phase_path.read_text().splitlines()] if phase_path.exists() else []
    complete = ['opt']
    for stage, label in labels.items():
        name = 'base_' + stage + '.dcp'
        if not (parent / name).is_file(): continue
        expected = pm.get('outputs', {}).get(name) or pm.get('recovery', {}).get('files', {}).get(name)
        if expected:
            if sha(parent / name) != expected: raise ValueError('Parent checkpoint hash changed: ' + name)
            complete.append(stage)
        elif any(len(row) >= 3 and row[0] == label and row[2] == '0' for row in phases):
            complete.append(stage)
    resume_stage = complete[-1] if a.from_stage == 'auto' else a.from_stage
    if resume_stage not in complete:
        raise ValueError('No successfully saved checkpoint for stage: ' + resume_stage)
    if a.preflight_only:
        print(json.dumps(dict(parent=str(parent), complete_checkpoints=complete,
             selected_stage=resume_stage, checkpoint_sha256=sha(parent/('base_'+resume_stage+'.dcp')),
             amf_rerun=False, binary_sha256=pm['binary_sha256'])))
        return 0
    out = Path(a.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'logs').mkdir(); (out / 'inputs').mkdir()
    script = Path(__file__).resolve().parent
    for f in script.iterdir():
        if f.is_file() and f.suffix in ('.py', '.tcl'):
            shutil.copy2(f, out / 'inputs' / f.name)
    job = copy.deepcopy(pm['job'])
    job.setdefault('static_routing_containment', True)  # Historical deployments enabled it.
    cfg = json.loads((parent / 'amf_config.json').read_text())
    inherited = ['base_opt.dcp', 'placement.tsv', 'modeled_sites.txt',
                 'hard_regions.tsv', 'hard_region_sites.tsv', 'amf_config.json']
    inherited += ['base_' + stage + '.dcp' for stage in complete if stage != 'opt'
                  and complete.index(stage) <= complete.index(resume_stage)]
    inherited += [f.name for f in parent.iterdir() if f.is_file() and
                  (f.suffix == '.tcldata' or f.name in ('static_containment.tsv',
                   'rp_footprints.tsv', 'containment_preflight_passed.txt',
                   'amf_profile.tsv', 'stage_intervals.tsv'))]
    files = {}
    for name in sorted(set(inherited)):
        shutil.copy2(parent / name, out / name)
        files[name] = sha(out / name)
        if files[name] != sha(parent / name):
            raise ValueError('Recovery copy mismatch: ' + name)
    job['resume_opt_checkpoint'] = str(out / 'base_opt.dcp')
    job['resume_full_stage'] = resume_stage
    job['prepare_worker_shells'] = False
    job['prepare_common_static_shell'] = True
    m = {k: copy.deepcopy(pm[k]) for k in ('schema', 'amf_placement_scope',
         'binary', 'binary_sha256', 'build', 'source_commit', 'comparison_identity',
         'input_hashes', 'full_input_coverage', 'full_placement_coverage',
         'primitive_counts', 'source_inputs') if k in pm}
    reused = [dict(s, execution='reused', source_run=s.get('source_run', str(parent)))
              for s in pm['stages'] if s['name'] in ('prepare', 'amf')]
    m.update(state='running', job=job, stages=reused, started_epoch=time.time(),
             dcp_storage='server-only', recovery=dict(parent=str(parent),
             parent_manifest_sha256=sha(parent / 'manifest.json'), files=files,
             reason=a.reason, checkpoint_stage=resume_stage,
             parent_backend_phase_times=phases,
             amf_rerun=False), script_hashes={f.name: sha(f) for f in (out/'inputs').iterdir()},
             timing_note='Reused stages retain original measurements; wall_seconds is this backend recovery only. Do not sum failed attempts into placement time.')
    def update(stage):
        save(out/'manifest.json', m)
        save(out/'status.json', dict(state=m['state'],stage=stage,error=m.get('error')))
    try:
        sp = settings(job, out, dict(modeled_sites=str(out/'modeled_sites.txt'),
                      dsp_critical=int(cfg.get('DSPCritical') == 'true')))
        save(out/'inputs/job.json', job)
        command = [job['vivado'], '-mode', 'batch', '-source',
                   str(out/'inputs/dfx_full_backend.tcl'), '-tclargs', str(sp)]
        update('backend'); start=time.time(); begin=time.monotonic()
        with (out/'logs/backend.log').open('w') as log:
            p=subprocess.Popen(command,cwd=out,stdout=log,stderr=subprocess.STDOUT)
            m['child_pid']=p.pid;update('backend');rc=p.wait()
        m['stages'].append(dict(name='backend',command=command,exit_code=rc,
              seconds=time.monotonic()-begin,started_epoch=start,ended_epoch=time.time(),execution='executed'))
        if rc: raise RuntimeError('Recovered backend failed; preserve this run')
        m['acceptance']=acceptance(out,'base')
        if not m['acceptance']['implementation_verified']:
            raise RuntimeError('Recovered routing/DRC acceptance failed')
        outputs=['base_imported.dcp','base_placed.dcp','base_routed.dcp','base_full.bit']
        outputs += [r['id']+'_partial.bit' for r in job['partitions']]
        m['common_static_shell']=record_common_static(job,out,out/'base_routed.dcp')
        outputs += common_static_artifacts(job)
        m['outputs']={n:sha(out/n) for n in outputs}
        m['state']='completed'
    except Exception as exc:
        m['state']='failed';m['error']=str(exc)
    finally:
        m['ended_epoch']=time.time();m['wall_seconds']=m['ended_epoch']-m['started_epoch'];update('finished')
    return 0 if m['state']=='completed' else 2


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','output'):p.add_argument('--'+key,required=True)
    p.add_argument('--from-stage', choices=['auto','opt','imported','placed','routed'], default='auto')
    p.add_argument('--reason', default='Resume from latest saved checkpoint after repairing backend diagnostics')
    p.add_argument('--preflight-only', action='store_true')
    raise SystemExit(run(p.parse_args()))
