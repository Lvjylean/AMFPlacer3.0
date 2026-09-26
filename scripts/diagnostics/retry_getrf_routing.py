#!/usr/bin/env python3
"""Retry GETRF routing from an audited placed DCP, preserving AMF provenance."""
import argparse
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from amf3 import save, stamp, machine
from inspect_amf_inputs import digest
from summarize_full_flow import summarize


def validate_source(source):
    manifest = json.loads((source / 'manifest.json').read_text())
    coverage = json.loads((source / 'reports/amf_coverage.json').read_text())
    placed = json.loads((source / 'reports/placed_placement.json').read_text())
    if not any(s['name'] == 'amf' and s['exit_code'] == 0 for s in manifest['stages']):
        raise ValueError('Source AMF has not completed')
    if coverage['input_cells'] != coverage['assigned_cells'] or coverage['missing_by_type']:
        raise ValueError('Source AMF export is incomplete')
    if not (placed['requested'] == placed['present'] == placed['placed'] == coverage['assigned_cells']):
        raise ValueError('Source placement is incomplete')
    for name in ('placed_cascades', 'placed_srl_cascades'):
        if json.loads((source / 'reports' / (name + '.json')).read_text())['violations']:
            raise ValueError('Source placement has illegal cascades')
    checkpoint = source / 'reports/getrf_placed.dcp'
    if not checkpoint.is_file():
        raise ValueError('Source placed DCP is absent')
    return manifest, checkpoint


def route_script(original):
    begin = original.index('    timed import {source [file join $placement import_placement.tcl]}')
    end = original.index('    timed route {route_design}')
    result = original[:begin] + '    timed place_audit {audit placed}\n' + original[end:]
    return result.replace('timed route {route_design}',
                          'timed route {route_design -directive AlternateCLBRouting}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--placement-run', required=True)
    args = parser.parse_args()
    source = (ROOT / args.placement_run).resolve()
    source_manifest, checkpoint = validate_source(source)
    directory = ROOT / 'experiments/runs' / ('getrf-u250-route-altclb-' + stamp())
    for name in ('inputs', 'reports', 'logs', 'placement', 'work'):
        (directory / name).mkdir(parents=True, exist_ok=False)
    shutil.copy2(source / 'config.json', directory / 'config.json')
    shutil.copy2(Path(__file__), directory / 'inputs/retry_getrf_routing.py')
    shutil.copy2(source / 'manifest.json', directory / 'inputs/placement_source_manifest.json')
    shutil.copy2(source / 'reports/stages.tsv', directory / 'inputs/placement_source_stages.tsv')
    for name in ('requested.tsv', 'bel_corrections.tsv', 'srl_cascades.tsv', 'resources.tsv', 'cascades.tsv', 'resources.json'):
        shutil.copy2(source / 'placement' / name, directory / 'placement' / name)
    inherited = []
    for name in ('amf_coverage', 'srl_cascades', 'imported_placement', 'imported_cascades', 'imported_srl_cascades'):
        origin = source / 'reports' / (name + '.json')
        shutil.copy2(origin, directory / 'reports' / origin.name)
        inherited.append(origin.name)
    backend = directory / 'inputs/full_backend.tcl'
    backend.write_text(route_script((ROOT / 'scripts/full_backend.tcl').read_text()))
    (directory / 'inputs/working_tree.patch').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=ROOT))
    manifest = dict(schema='amf-route-retry-v1', source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        started=dt.datetime.now().astimezone().isoformat(), stages=[], placement_run=str(source),
        placement_source_manifest_sha256=digest(directory / 'inputs/placement_source_manifest.json'),
        inherited_stage_times=str(directory / 'inputs/placement_source_stages.tsv'), inherited_reports=inherited,
        router_input_dcp=str(checkpoint), router_input_dcp_sha256=digest(checkpoint),
        original_input_dcp=source_manifest['input_dcp'], original_input_dcp_sha256=source_manifest['input_dcp_sha256'],
        placement_binary=source_manifest['binary'], placement_binary_sha256=source_manifest['binary_sha256'],
        directive='AlternateCLBRouting', backend_script_sha256=digest(backend),
        config_sha256=digest(directory / 'config.json'), amf_clock_period_ns=source_manifest['amf_clock_period_ns'],
        vivado_clock_source='constraints retained in audited placed DCP', dcp_storage='server-only')
    save(directory / 'manifest.json', manifest)
    print(directory, flush=True)
    try:
        save(directory / 'status.json', dict(state='running', stage='vivado', placement_reused=True))
        command = [machine()['vivado'], '-mode', 'batch', '-notrace', '-nojournal', '-log', str(directory / 'logs/vivado_internal.log'),
                   '-source', str(backend), '-tclargs', str(checkpoint), str(directory / 'reports'), str(directory / 'placement')]
        begin = time.monotonic()
        with (directory / 'logs/vivado.log').open('w') as log:
            result = subprocess.run(command, cwd=directory / 'work', stdout=log, stderr=subprocess.STDOUT)
        manifest['stages'].append(dict(name='vivado_route_retry', command=command, elapsed_seconds=time.monotonic()-begin, exit_code=result.returncode))
        save(directory / 'manifest.json', manifest)
        if result.returncode:
            raise RuntimeError('Vivado routing retry failed; see logs/vivado.log')
        summary = summarize(directory)
        save(directory / 'reports/summary.json', summary)
        save(directory / 'status.json', dict(state='completed', placement_reused=True, full_placement_executed=False,
            routing_executed=True, routing_complete=summary['routing_complete'], drc_errors=summary['drc_errors'],
            timing_met=summary['timing_met'], amf_export_complete=summary['amf_export_complete'],
            implementation_verified=summary['implementation_verified'],
            output_dcp_sha256=digest(directory / 'reports/getrf_routed.dcp'), finished=dt.datetime.now().astimezone().isoformat()))
    except Exception as error:
        save(directory / 'status.json', dict(state='failed', error=str(error), finished=dt.datetime.now().astimezone().isoformat()))
        raise


if __name__ == '__main__':
    main()
