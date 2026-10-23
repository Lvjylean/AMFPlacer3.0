#!/usr/bin/env python3
"""Run a native Vivado control using an immutable AMF experiment's inputs."""
import argparse
import datetime as dt
import difflib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from run_face_detect_flow import digest, now, save_json, stage
from summarize_face_detect_flow import summarize


TIMING_TCL = r'''
proc flow_timed {label script} {
    set started [clock milliseconds]
    set code [catch {uplevel 1 $script} value opts]
    set seconds [expr {([clock milliseconds] - $started) / 1000.0}]
    puts $::flowTiming "$label\t$seconds\t$code"
    flush $::flowTiming
    puts "FLOW_SECONDS $label $seconds status=$code"
    if {$code} {return -options $opts $value}
    return $value
}
'''


def native_script(reference_text, placement_text):
    commands = re.findall(r'^(?:place_design|route_design)\b[^\n]*', placement_text, re.M)
    if commands != ['place_design -unplace', 'place_design', 'route_design']:
        raise ValueError('Reference implementation commands changed: ' + repr(commands))
    if reference_text.count('    source $placementTcl\n') != 1:
        raise ValueError('Expected exactly one AMF placement import in reference script')
    text = reference_text.replace('file mkdir $outDir\n',
        'file mkdir $outDir\nset flowTiming [open "${outDir}/stage_times.tsv" w]\n'
        'puts $flowTiming "stage\\tseconds\\ttcl_status"\n' + TIMING_TCL)
    text = text.replace('    source $placementTcl\n', '\n'.join(
        f'    flow_timed {label} {{{command}}}' for label, command in
        [('unplace', commands[0]), ('place_design', commands[1]), ('route_design', commands[2])]) + '\n')
    text = text.replace('    open_checkpoint $inputDcp\n', '    flow_timed open_checkpoint {open_checkpoint $inputDcp}\n')
    text = text.replace('faceDetect_amf_routed.dcp', 'faceDetect_vivado_routed.dcp')
    text = text.replace('    write_checkpoint "${outDir}/faceDetect_vivado_routed.dcp"',
                        '    flow_timed write_checkpoint {write_checkpoint "${outDir}/faceDetect_vivado_routed.dcp"}')
    text = text.replace('puts "FLOW_IMPORT_START"', 'puts "FLOW_NATIVE_IMPLEMENTATION_START"')
    text = text.replace('puts "FLOW_BACKEND_OK"', 'close $flowTiming\nputs "FLOW_BACKEND_OK"')
    return text


def command_seconds(log):
    result = {}
    pattern = re.compile(r'^(open_checkpoint|place_design|route_design|write_checkpoint):.*?elapsed = (\d+):(\d+):(\d+(?:\.\d+)?)')
    with Path(log).open(errors='replace') as stream:
        for line in stream:
            match = pattern.search(line)
            if match:
                result[match[1]] = int(match[2]) * 3600 + int(match[3]) * 60 + float(match[4])
    return result


def compare(root, summary):
    manifest = json.loads((root / 'manifest.json').read_text())
    reference = Path(manifest['reference_run'])
    amf = json.loads((reference / 'reports/verification_summary.json').read_text())
    amf_manifest = json.loads((reference / 'manifest.json').read_text())
    amf_stages = {x['stage']: x['elapsed_seconds'] for x in amf_manifest['stages']}
    native_elapsed = manifest['stages'][0]['elapsed_seconds']
    amf_total = sum(amf_stages.values())
    return {
        'reference_run': str(reference), 'native_run': str(root),
        'same_input_sha256': manifest['dcp_sha256'] == amf_manifest['dcp_sha256'],
        'same_vivado_version': manifest['vivado_version'] == amf_manifest['vivado_version'],
        'vivado_threads': manifest['threads']['vivado_threads'],
        'implementation_commands': ['place_design -unplace', 'place_design', 'route_design'],
        'constraints': 'Both Vivado backends retain the same input DCP constraints; AMF separately uses its saved user ClockPeriod=15 ns target.',
        'amf_stage_seconds': amf_stages, 'amf_total_stage_seconds': amf_total,
        'native_total_stage_seconds': native_elapsed,
        'amf_to_native_total_time_ratio': amf_total / native_elapsed,
        'amf_without_export_seconds': amf_stages['02_amf'] + amf_stages['03_vivado_route'],
        'amf_command_log_seconds_rounded': command_seconds(reference / 'logs/03_vivado_route.log'),
        'native_command_log_seconds_rounded': command_seconds(root / 'logs/01_vivado_native.log'),
        'amf_timing': amf['timing_summary.rpt'], 'native_timing': summary['timing_summary.rpt'],
        'amf_bus_skew': amf['bus_skew'], 'native_bus_skew': summary['bus_skew'],
        'amf_routing': amf['routing_counts'], 'native_routing': summary['routing_counts'],
        'amf_drc': amf['drc.rpt'], 'native_drc': summary['drc.rpt'],
        'measurement_scope': 'Wall-clock subprocess stages including identical backend input-name/type checks, pre/post reports and final DCP write. Setup snapshot copies and subsequent audit excluded. Single run each; no RTL synthesis or bitstream.',
        'limitations': ['AMF result import was not independently timed in the reference run.',
                       'AMF plus its subsequent Vivado placement is not equivalent to measuring the AMF executable alone.',
                       'The without-export subtotal assumes a pre-existing valid input export; it is not measured incremental compilation.']}


def worker(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    try:
        command = [manifest['vivado'], '-mode', 'batch', '-notrace', '-source', root / 'scripts/native.tcl',
                   '-tclargs', root / 'inputs/reference.dcp', root / 'scripts/native.tcl',
                   root / 'reports', root / 'inputs/expected_cells.tsv']
        stage(root, manifest, '01_vivado_native', command, root / 'work/vivado', 10800)
        output = root / 'reports/faceDetect_vivado_routed.dcp'
        report = (root / 'reports/route_status.rpt').read_text()
        errors = re.search(r'# of nets with routing errors[^:]*:\s*(\d+)', report, re.I)
        if not output.is_file() or not errors or int(errors[1]):
            raise RuntimeError('Native run has no final checkpoint or zero-routing-error confirmation')
        save_json(root / 'status.json', {'state': 'completed', 'updated': now(), 'pid': os.getpid(),
                                        'routed_dcp': str(output), 'routing_errors': 0})
        summary = summarize(root)
        summary['artifacts'][output.name] = {'bytes': output.stat().st_size, 'sha256': digest(output)}
        summary['command_seconds_precise'] = {
            row[0]: float(row[1]) for row in
            (line.split('\t') for line in (root / 'reports/stage_times.tsv').read_text().splitlines()[1:])}
        save_json(root / 'reports/verification_summary.json', summary)
        save_json(root / 'reports/amf_comparison.json', compare(root, summary))
    except Exception as error:
        save_json(root / 'status.json', {'state': 'failed', 'updated': now(), 'pid': os.getpid(), 'error': str(error)})
        raise


def launch(repo, reference_id, dry_run=False):
    reference = repo / 'experiments/runs' / reference_id
    previous = json.loads((reference / 'manifest.json').read_text())
    if json.loads((reference / 'status.json').read_text())['state'] != 'completed':
        raise RuntimeError('Reference AMF experiment must be completed')
    source = reference / 'inputs/reference.dcp'
    if digest(source) != previous['dcp_sha256']:
        raise RuntimeError('Reference input DCP no longer matches saved hash')
    script = native_script((reference / 'scripts/route.tcl').read_text(),
                           (reference / 'placement/placement_for_vivado.tcl').read_text())
    version = subprocess.check_output([previous['vivado'], '-version'], text=True).strip()
    if version != previous['vivado_version']:
        raise RuntimeError('Vivado version differs from reference run')
    if dry_run:
        print(json.dumps({'reference': str(reference), 'dcp_sha256': previous['dcp_sha256'],
                          'vivado': previous['vivado'], 'threads': previous['threads']['vivado_threads'],
                          'commands': ['place_design -unplace', 'place_design', 'route_design']}))
        return
    run_id = 'faceDetect-vivado-native-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    root = repo / 'experiments/runs' / run_id
    root.mkdir(parents=True, exist_ok=False)
    for name in ['inputs', 'scripts', 'reports', 'logs', 'provenance', 'work/vivado']:
        (root / name).mkdir(parents=True)
    shutil.copy2(source, root / 'inputs/reference.dcp')
    if digest(root / 'inputs/reference.dcp') != previous['dcp_sha256']:
        raise RuntimeError('Native input snapshot hash mismatch')
    shutil.copy2(reference / 'inputs/expected_cells.tsv', root / 'inputs/expected_cells.tsv')
    for name in ['run_native_face_detect.py', 'run_face_detect_flow.py', 'summarize_face_detect_flow.py']:
        shutil.copy2(repo / 'scripts' / name, root / 'scripts' / name)
    for name in ['manifest.json', 'config.json']:
        shutil.copy2(reference / name, root / 'provenance' / ('amf_' + name))
    shutil.copy2(reference / 'scripts/route.tcl', root / 'provenance/amf_route.tcl')
    (root / 'scripts/native.tcl').write_text(script)
    (root / 'provenance/native_vs_amf_route.diff').write_text(''.join(difflib.unified_diff(
        (reference / 'scripts/route.tcl').read_text().splitlines(True), script.splitlines(True),
        fromfile='AMF backend', tofile='native backend')))
    git = lambda *args: subprocess.check_output(['git', *args], cwd=repo, text=True).strip()
    manifest = {'created': now(), 'host': os.uname().nodename, 'case': 'faceDetect',
                'input_mode': 'vivado-native', 'repo': str(repo), 'run_directory': str(root),
                'reference_run': str(reference), 'input_dcp_source': str(source),
                'dcp_sha256': previous['dcp_sha256'], 'vivado': previous['vivado'],
                'vivado_version': version, 'threads': {'vivado_threads': previous['threads']['vivado_threads']},
                'source_commit': git('rev-parse', 'HEAD'), 'git_status': git('status', '--short'),
                'timing_policy': 'Unmodified constraints in the same input DCP',
                'script_sha256': digest(root / 'scripts/native.tcl'), 'stages': []}
    (root / 'provenance/working_tree.patch').write_text(git('diff', 'HEAD', '--binary'))
    (root / 'provenance/server_load.txt').write_text(subprocess.check_output(['uptime'], text=True))
    save_json(root / 'manifest.json', manifest)
    save_json(root / 'config.json', {'reference_run': reference_id, 'threads': manifest['threads'],
                                    'commands': ['place_design -unplace', 'place_design', 'route_design'],
                                    'dcp_storage': 'server-only'})
    validation = json.loads((reference / 'reports/input_validation.json').read_text())
    save_json(root / 'reports/input_validation.json', {
        'same_input_dcp_sha256': True, 'expected_cells': validation['new_cells'],
        'design_state': validation['design_state'],
        'runtime_check': 'Native Tcl checks all expected names and types before implementation'})
    save_json(root / 'status.json', {'state': 'prepared', 'updated': now()})
    environment = dict(os.environ, AMF_PROJECT_ROOT=str(repo))
    with (root / 'logs/runner.log').open('w') as log:
        process = subprocess.Popen([sys.executable, root / 'scripts/run_native_face_detect.py', '--worker', root],
                                   cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (root / 'runner.pid').write_text(str(process.pid) + '\n')
    registry_path = repo / 'experiments/registry.json'
    registry = json.loads(registry_path.read_text())
    registry['runs'].append({'id': run_id, 'kind': 'vivado-native-control', 'reference_run': reference_id,
                             'directory': 'experiments/runs/' + run_id,
                             'status_file': 'experiments/runs/' + run_id + '/status.json', 'created': now()})
    save_json(registry_path, registry)
    print(json.dumps({'run_id': run_id, 'run_directory': str(root), 'pid': process.pid}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', type=Path, required=True)
    worker(parser.parse_args().worker.resolve())
