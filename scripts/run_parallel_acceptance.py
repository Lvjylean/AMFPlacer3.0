#!/usr/bin/env python3
"""Parallel read-only final checks followed by one full+partial bitstream write."""
import argparse
import datetime
import json
from pathlib import Path
import shutil
import time
from run_dfx import save, sha
import run_dfx_multi as flow
from parallel_runtime import command, parallel_barrier, require_memory, verify_hashes
from timing_coverage import verify as verify_timing_coverage

SCRIPTS = Path(__file__).resolve().parent


def source_contract(source):
    source = Path(source).resolve()
    m = json.loads((source/'manifest.json').read_text())
    if m.get('state') not in ('completed', 'routed_awaiting_verification'):
        raise ValueError('Source must be a stopped, successfully routed joint design')
    job = m['job']
    if job['backend_place_mode'] != 'route-only' or job['vivado_threads'] != 4 or float(job['clock_period_ns']) != float(job.get('experiment_clock_period_ns', 20)):
        raise ValueError('Unexpected source conditions')
    if job['static_routing_containment']:
        raise ValueError('Unexpected static containment')
    routed = source/'joint_routed.dcp'
    expected = m['outputs']['joint_routed.dcp']['sha256']
    if sha(routed) != expected:
        raise ValueError('Routed source differs from its producer manifest')
    reference = Path(job['shell_run'])/'base_routed.dcp'
    parent = json.loads((Path(job['shell_run'])/'manifest.json').read_text())
    expected_ref = parent['outputs']['base_routed.dcp']
    if isinstance(expected_ref, dict): expected_ref = expected_ref['sha256']
    if sha(reference) != expected_ref:
        raise ValueError('Static reference differs from its producer manifest')
    files = [source/'manifest.json', routed, reference, source/'imported_rp_route_footprints.tcldata']
    for part in job['partitions']:
        k = part['id']
        files += [source/'inputs'/('worker_'+k)/n for n in ('placement.tsv', 'blocked_sites.txt')]
        if job.get('native_local_pin_access_policy','off')!='off':
            files.append(source/'inputs'/('worker_'+k)/'lut_pin_assignments.tsv')
        files += [source/'inputs/common_static'/(k+'_common_static_boundary.tcldata')]
    if job.get('static_placement_audit') == 'full': files.append(source/'static_snapshot.tcldata')
    hashes = {str(p): sha(p) for p in files}
    return source, job, routed, reference, hashes


def run(source, output):
    started = time.monotonic()
    started_epoch = time.time()
    source, job, routed, reference, hashes = source_contract(source)
    out = Path(output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    state = dict(schema='amf-parallel-acceptance-v1', state='preflight',
                 started_epoch=started_epoch, started_at=datetime.datetime.now().astimezone().isoformat(),
                 source_joint=str(source), immutable_inputs=hashes, branches={},
                 placement_or_routing_executed=False, monitoring_restarted=False)
    save(out/'manifest.json', state)
    try:
        state['resources'] = require_memory(80)
        frozen = out/'frozen_scripts'; frozen.mkdir()
        for p in SCRIPTS.glob('*.tcl'): shutil.copy2(p, frozen/p.name)
        state['script_hashes'] = {p.name: sha(p) for p in frozen.iterdir()}
        save(out/'manifest.json', state)

        def native(mode, script):
            branch = out/mode; (branch/'inputs').mkdir(parents=True)
            for p in frozen.iterdir(): shutil.copy2(p, branch/'inputs'/p.name)
            if job.get('static_placement_audit') == 'full':
                shutil.copy2(source/'static_snapshot.tcldata', branch/'static_snapshot.tcldata')
            extra = dict(verification_mode=mode, routed_checkpoint=str(routed), reference_checkpoint=str(reference),
                         imported_footprints=str(source/'imported_rp_route_footprints.tcldata'),
                         common_static_metadata=str(source/'inputs/common_static'))
            for p in job['partitions']:
                k=p['id']; extra['worker_'+k]=str(source/'inputs'/('worker_'+k))
                with (source/'inputs'/('worker_'+k)/'placement.tsv').open() as f:
                    extra['expected_cells_'+k]=sum(bool(line.strip()) for line in f)
            config=flow.settings(job, branch, extra)
            verify_hashes(hashes)
            args=['/usr/bin/time','-v','-o',str(branch/'process-resource.txt'),job['vivado'], '-mode','batch',
                  '-source',str(branch/'inputs'/script),'-tclargs',str(config)]
            result=command(args, branch/'exec', timeout=7200)
            verify_hashes(hashes)
            if mode == 'qor':
                acceptance=flow.acceptance(branch, 'joint')
                if job.get('clock_contract_enabled'):
                    acceptance['timing_coverage']=verify_timing_coverage(branch/'joint_timing.rpt',job['clock_timing_baseline'])
                if not acceptance['implementation_verified'] or not acceptance['timing_met']:
                    raise RuntimeError('Final QoR/default DRC/route acceptance failed')
                result['acceptance']=acceptance
            elif mode == 'static':
                if (branch/'pr_verify_passed.txt').read_text().strip()!='full_check_passed':
                    raise RuntimeError('Missing successful full_check marker')
                result['pr_verify_full_check']=True
            return result

        state['state']='parallel_verification'; save(out/'manifest.json',state)
        begin=time.monotonic()
        results=parallel_barrier({k:lambda k=k:native(k,'dfx_parallel_verify.tcl') for k in ('qor','static')})
        state['branches']=results
        state['parallel_verification_wall_seconds']=time.monotonic()-begin
        state['native_process_span_seconds']=max(v['ended_epoch'] for v in results.values())-min(v['started_epoch'] for v in results.values())
        state['native_process_overlap_seconds']=max(0,min(v['ended_epoch'] for v in results.values())-max(v['started_epoch'] for v in results.values()))
        verify_hashes(hashes)
        state['state']='verified_bitstreams'; state['all_checks_passed_epoch']=time.time(); save(out/'manifest.json',state)
        state['bitstreams']=native('bitstreams','dfx_verified_bitstream.tcl')
        mapping={}
        for line in (out/'bitstreams/partial_mapping.tsv').read_text().splitlines():
            k,rp,pb,path=line.split('\t')
            if k in mapping: raise ValueError('Duplicate partial bitstream mapping')
            part=next(p for p in job['partitions'] if p['id']==k)
            if (rp,pb)!=(part['active_partition'],part['pblock']): raise ValueError('Incorrect partial mapping')
            expected=out/'bitstreams'/('full_'+pb+'_partial.bit')
            if Path(path)!=expected or not expected.stat().st_size: raise ValueError('Incorrect partial artifact')
            mapping[k]=dict(partition=rp,pblock=pb,path=path,sha256=sha(expected),bytes=expected.stat().st_size)
        if set(mapping)!={p['id'] for p in job['partitions']}: raise ValueError('Incomplete partial outputs')
        full=out/'bitstreams/full.bit'
        if not full.stat().st_size: raise ValueError('Empty full bitstream')
        state['outputs']=dict(full=dict(path=str(full),sha256=sha(full),bytes=full.stat().st_size),partials=mapping)
        verify_hashes(hashes)
        state.update(state='completed', input_hashes_unchanged=True, acceptance=results['qor']['acceptance'],
                     pr_verify_full_check=True, explicit_partial_regeneration=False)
    except Exception as error:
        state.update(state='failed',error=repr(error))
        raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-started)
        save(out/'manifest.json',state)
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-joint',required=True); p.add_argument('--output',required=True)
    a=p.parse_args(); result=run(a.source_joint,a.output)
    from run_auxiliary_reports import dispatch_after_completion
    dispatch_after_completion(a.source_joint,result/'background_reports',result/'manifest.json')
    print(result)
