#!/usr/bin/env python3
"""Run an isolated DCP -> AMFPlacer -> Vivado route experiment on eda072."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

REPO = Path(os.environ.get('AMF_PROJECT_ROOT', Path(__file__).resolve().parents[1]))
VIVADO = os.environ.get('AMF_VIVADO', '/Projects/Xilinx/Vivado/2024.2/bin/vivado')
DCP = Path(os.environ.get('AMF_REFERENCE_DCP', str(REPO / 'data/reference/faceDetect/reference.dcp')))
AMF_JOBS = int(os.environ.get('AMF_JOBS', '8'))
VIVADO_THREADS = int(os.environ.get('AMF_VIVADO_THREADS', '4'))


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def save_json(path, data):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    temporary.replace(path)


def now():
    return dt.datetime.now().astimezone().isoformat()


EXPORT_TCL = r'''
set_param general.maxThreads 4
set inputDcp [lindex $argv 0]
set targetFolderPath [file normalize [lindex $argv 1]]
set scriptFolder [file normalize [lindex $argv 2]]
file mkdir $targetFolderPath
if {[catch {
    open_checkpoint $inputDcp
    set part [get_property PART [current_design]]
    if {$part ne "xcvu095-ffva2104-2-e"} {error "Unexpected part: $part"}
    puts "FLOW_PART=$part"
    set pahtPrefix "${targetFolderPath}/faceDetect_"
    source "${scriptFolder}/extractNetlist.tcl"
    source "${scriptFolder}/extractLUTRAMs.tcl"
    source "${scriptFolder}/extractFixedUnits.tcl"
    puts "FLOW_EXPORTED_CELLS=[llength $allCells]"
    exec zip -q -j "${pahtPrefix}allCellPinNet.zip" "${pahtPrefix}allCellPinNet"
    file delete "${pahtPrefix}allCellPinNet"
    close_design
} msg opts]} {
    puts "FLOW_EXPORT_FAILED=$msg"
    puts [dict get $opts -errorinfo]
    exit 10
}
puts "FLOW_EXPORT_OK"
exit 0
'''

ROUTE_TCL = r'''
set_param general.maxThreads 4
set inputDcp [lindex $argv 0]
set placementTcl [file normalize [lindex $argv 1]]
set outDir [file normalize [lindex $argv 2]]
set expectedCellsFile [lindex $argv 3]
file mkdir $outDir
if {[catch {
    open_checkpoint $inputDcp
    if {$expectedCellsFile ne ""} {
        set expected [dict create]
        set f [open $expectedCellsFile r]
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] name type
            dict set expected $name $type
        }
        close $f
        set matched 0
        foreach cell [xilinx::designutils::get_leaf_cells *] {
            if {[dict exists $expected $cell]} {
                if {[get_property REF_NAME $cell] ne [dict get $expected $cell]} {
                    error "Cell type mismatch for $cell"
                }
                incr matched
            }
        }
        puts "FLOW_DCP_MATCHED_CELLS=$matched/[dict size $expected]"
        if {$matched != [dict size $expected]} {error "Benchmark cells missing from DCP"}
    }
    report_timing_summary -file "${outDir}/reference_timing_summary.rpt"
    report_drc -file "${outDir}/reference_drc.rpt"
    puts "FLOW_IMPORT_START"
    source $placementTcl
    puts "FLOW_ROUTING_FINISHED"
    report_route_status -file "${outDir}/route_status.rpt"
    report_drc -file "${outDir}/drc.rpt"
    report_timing_summary -file "${outDir}/timing_summary.rpt"
    report_utilization -file "${outDir}/utilization.rpt"
    report_bus_skew -file "${outDir}/bus_skew.rpt"
    write_checkpoint "${outDir}/faceDetect_amf_routed.dcp"
    puts "FLOW_ROUTED_DCP_WRITTEN"
    close_design
} msg opts]} {
    puts "FLOW_BACKEND_FAILED=$msg"
    puts [dict get $opts -errorinfo]
    exit 20
}
puts "FLOW_BACKEND_OK"
exit 0
'''


def prepare(root, input_mode='export', binary_dir=None):
    binary_dir = (binary_dir or REPO / 'builds/current').resolve()
    root.mkdir(parents=True, exist_ok=False)
    for name in ['bin', 'inputs/baseline', 'inputs/exported', 'scripts', 'logs', 'reports', 'work/export', 'work/amf', 'work/vivado', 'placement', 'provenance']:
        (root / name).mkdir(parents=True, exist_ok=True)
    shutil.copy2(__file__, root / 'scripts/run_flow.py')
    manifest = {'created': now(), 'host': os.uname().nodename, 'case': 'faceDetect',
                'input_mode': input_mode,
                'scope': ('existing benchmark -> existing AMFPlacer binary -> Vivado placement and routing using matching DCP; no fresh export, RTL synthesis or bitstream'
                          if input_mode == 'benchmark' else
                          'routed input DCP -> fresh export -> existing AMFPlacer binary -> Vivado placement and routing; no RTL synthesis or bitstream'),
                'repo': str(REPO), 'vivado': VIVADO, 'input_dcp_source': str(DCP),
                'binary_directory': str(binary_dir),
                'binary_source_correspondence': ('Existing repository binary and current source diff recorded; correspondence not independently verified.'
                                                if binary_dir == (REPO / 'build').resolve() else
                                                'Explicit binary directory selected; see binary_directory and any rebuild_provenance.json for build details.'),
                'inputs': {}, 'stages': [], 'run_directory': str(root)}
    shutil.copy2(DCP, root / 'inputs/reference.dcp')
    manifest['dcp_sha256'] = digest(root / 'inputs/reference.dcp')
    for name in ['AMFPlacer', 'partitionHyperGraph']:
        src = binary_dir / name
        shutil.copy2(src, root / 'bin' / name)
        manifest[name + '_sha256'] = digest(src)
    build_manifest = binary_dir.parent / 'manifest.json'
    if build_manifest.is_file():
        build_record = json.loads(build_manifest.read_text())
        for name in ['AMFPlacer', 'partitionHyperGraph']:
            if build_record.get('binaries', {}).get(name) != manifest[name + '_sha256']:
                raise RuntimeError('Binary differs from build manifest: ' + name)
        shutil.copy2(build_manifest, root / 'provenance/build_manifest.json')
        shutil.copy2(binary_dir.parent / 'source_hashes.json', root / 'provenance/build_source_hashes.json')
        manifest['build_id'] = build_record['build_id']
        manifest['binary_source_correspondence'] = 'Binary hashes match the saved build manifest and source snapshot.'
    manifest['threads'] = {'amf_jobs': AMF_JOBS, 'vivado_threads': VIVADO_THREADS}
    manifest['vivado_version'] = subprocess.check_output([VIVADO, '-version'], text=True).strip()
    for name in ['extractNetlist.tcl', 'extractLUTRAMs.tcl', 'extractFixedUnits.tcl']:
        shutil.copy2(REPO / 'benchmarks/vivadoScripts' / name, root / 'scripts' / name)
    config_source = REPO / 'benchmarks/testConfig/faceDetect.json'
    shutil.copy2(config_source, root / 'provenance/original_config.json')
    config = dict(re.findall(r'^\s*"([^"]+)"\s*:\s*"([^"]*)"', config_source.read_text(), re.M))
    for key, value in list(config.items()):
        if value.startswith('../benchmarks/'):
            source = (REPO / 'build' / value).resolve()
            target = root / 'inputs/baseline' / source.name
            shutil.copy2(source, target)
            manifest['inputs'][key] = {'source': str(source), 'snapshot': str(target), 'sha256': digest(target)}
            config[key] = str(target)
    new_inputs = {'vivado extracted design information file': 'allCellPinNet.zip',
                  'unpredictable macro file': 'unpredictableMacros',
                  'fixed units file': 'fixedUnits', 'clock file': 'clocks'}
    if input_mode == 'export':
        for key, suffix in new_inputs.items():
            config[key] = str(root / 'inputs/exported' / ('faceDetect_' + suffix))
    config['dumpDirectory'] = str(root / 'placement')
    config['jobs'] = str(AMF_JOBS)
    save_json(root / 'config.json', config)
    manifest['source_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip()
    for args, filename in [(['status', '--short'], 'git_status.txt'), (['diff', 'HEAD', '--binary'], 'working_tree.patch')]:
        (root / 'provenance' / filename).write_bytes(subprocess.check_output(['git', *args], cwd=REPO))
    for name in ['MLTimingModel.h', 'MLTimingModel.cc']:
        source = REPO / 'src/lib/HiFPlacer/placement/placementTiming' / name
        if source.exists():
            shutil.copy2(source, root / 'provenance' / name)
    (root / 'scripts/export.tcl').write_text(EXPORT_TCL.replace('general.maxThreads 4', f'general.maxThreads {VIVADO_THREADS}'))
    (root / 'scripts/route.tcl').write_text(ROUTE_TCL.replace('general.maxThreads 4', f'general.maxThreads {VIVADO_THREADS}'))
    save_json(root / 'manifest.json', manifest)
    save_json(root / 'status.json', {'state': 'prepared', 'updated': now(), 'run_directory': str(root)})
    return manifest


def netlist_cells(path):
    cells = {}
    with zipfile.ZipFile(path) as archive:
        with archive.open(archive.namelist()[0]) as stream:
            for line in stream:
                if line.startswith(b'curCell=>'):
                    match = re.match(r'curCell=>\s+(\S+)\s+type=>\s+(\S+)', line.decode())
                    if match:
                        cells[match[1]] = match[2]
    return cells


def validate_export(root):
    new = netlist_cells(root / 'inputs/exported/faceDetect_allCellPinNet.zip')
    old = netlist_cells(root / 'inputs/baseline/faceDetect_allCellPinNet.zip')
    if not new:
        raise RuntimeError('Exported netlist is empty')
    referenced = set()
    with zipfile.ZipFile(root / 'inputs/baseline/faceDetect_clusters.zip') as archive:
        with archive.open(archive.namelist()[0]) as stream:
            for line in stream:
                referenced.update(line.decode().split())
    missing = sorted(referenced - new.keys())
    validation = {'new_cells': len(new), 'old_cells': len(old), 'common_names': len(new.keys() & old.keys()),
                  'cluster_referenced_cells': len(referenced), 'cluster_missing_cells': len(missing), 'missing_examples': missing[:10],
                  'exported_files': {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (root / 'inputs/exported').iterdir() if p.is_file()}}
    save_json(root / 'reports/input_validation.json', validation)
    if missing:
        raise RuntimeError(f'Historical cluster file references {len(missing)} absent cells; see input_validation.json')
    for name in ['faceDetect_unpredictableMacros', 'faceDetect_fixedUnits', 'faceDetect_clocks']:
        if not (root / 'inputs/exported' / name).is_file():
            raise RuntimeError('Missing exported input: ' + name)


def validate_benchmark(root, manifest):
    for entry in manifest['inputs'].values():
        if digest(entry['snapshot']) != entry['sha256']:
            raise RuntimeError('Benchmark snapshot hash mismatch: ' + entry['snapshot'])
    base = root / 'inputs/baseline'
    cells = netlist_cells(base / 'faceDetect_allCellPinNet.zip')
    if not cells:
        raise RuntimeError('Benchmark netlist is empty')
    with zipfile.ZipFile(base / 'faceDetect_clusters.zip') as archive:
        referenced = set(archive.read(archive.namelist()[0]).decode().split())
    missing = sorted(referenced - cells.keys())
    macro_rows = [line.split() for line in (base / 'faceDetect_unpredictableMacros').read_text().splitlines()
                  if line.startswith('name=>')]
    macro_names = {row[1] for row in macro_rows}
    missing_macros = sorted(macro_names - cells.keys())
    validation = {'mode': 'benchmark', 'cells': len(cells), 'snapshot_files': len(manifest['inputs']),
                  'snapshot_hashes_verified': True, 'cluster_referenced_cells': len(referenced),
                  'cluster_missing_cells': len(missing), 'macro_rows': len(macro_rows),
                  'macro_unique_cells': len(macro_names), 'macro_missing_cells': len(missing_macros),
                  'missing_examples': (missing + missing_macros)[:10],
                  'macro_input_policy': 'Unmodified historical file, including its header convention; the existing loader skips the first line.'}
    save_json(root / 'reports/input_validation.json', validation)
    if missing or missing_macros:
        raise RuntimeError('Benchmark references absent cells; see input_validation.json')
    (root / 'inputs/expected_cells.tsv').write_text(''.join(f'{name}\t{kind}\n' for name, kind in cells.items()))


def stage(root, manifest, name, command, cwd, timeout):
    entry = {'stage': name, 'command': list(map(str, command)), 'cwd': str(cwd), 'started': now()}
    manifest['stages'].append(entry)
    save_json(root / 'manifest.json', manifest)
    save_json(root / 'status.json', {'state': 'running', 'stage': name, 'pid': os.getpid(), 'updated': now(), 'log': str(root / 'logs' / (name + '.log'))})
    started = time.monotonic()
    with (root / 'logs' / (name + '.log')).open('w') as output:
        result = subprocess.run(entry['command'], cwd=cwd, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
    entry.update({'finished': now(), 'elapsed_seconds': round(time.monotonic() - started, 3), 'exit_code': result.returncode})
    save_json(root / 'manifest.json', manifest)
    if result.returncode:
        raise RuntimeError(f'{name} exited with code {result.returncode}; see logs/{name}.log')


def worker(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    try:
        if manifest.get('input_mode') == 'benchmark':
            validate_benchmark(root, manifest)
        else:
            stage(root, manifest, '01_export', [VIVADO, '-mode', 'batch', '-notrace', '-source', root / 'scripts/export.tcl', '-tclargs', root / 'inputs/reference.dcp', root / 'inputs/exported', root / 'scripts'], root / 'work/export', 3600)
            validate_export(root)
        stage(root, manifest, '02_amf', [root / 'bin/AMFPlacer', root / 'config.json'], root / 'work/amf', 7200)
        generated = root / 'placement/DumpCLBPacking-first-0.tcl'
        if not generated.is_file():
            raise RuntimeError('AMFPlacer did not produce placement Tcl')
        # Existing exporter emits "$errorNum" as a command, which Tcl interprets
        # as a command name (e.g. "0"). Fix this one diagnostic line in the
        # run-local copy, preserving the original output and placement commands.
        text = generated.read_text()
        adapted, count = re.subn(r'^\$errorNum\s*$', 'puts "FLOW_INITIAL_PLACEMENT_ERROR_BATCHES=$errorNum"', text, flags=re.M)
        adapter = root / 'placement/placement_for_vivado.tcl'
        adapter.write_text(adapted)
        save_json(root / 'reports/tcl_adapter.json', {'original': str(generated), 'adapted': str(adapter), 'diagnostic_lines_fixed': count, 'original_sha256': digest(generated), 'adapted_sha256': digest(adapter), 'placement_algorithm_changed': False})
        expected_cells = root / 'inputs/expected_cells.tsv'
        command = [VIVADO, '-mode', 'batch', '-notrace', '-source', root / 'scripts/route.tcl', '-tclargs', root / 'inputs/reference.dcp', adapter, root / 'reports']
        if expected_cells.is_file():
            command.append(expected_cells)
        stage(root, manifest, '03_vivado_route', command, root / 'work/vivado', 10800)
        if not (root / 'reports/faceDetect_amf_routed.dcp').is_file():
            raise RuntimeError('Missing final routed DCP')
        route_report = (root / 'reports/route_status.rpt').read_text()
        routing_errors = re.search(r'# of nets with routing errors[^:]*:\s*(\d+)', route_report, re.I)
        if not routing_errors or int(routing_errors[1]):
            raise RuntimeError('Route report missing zero-error confirmation; inspect reports')
        save_json(root / 'status.json', {'state': 'completed', 'updated': now(), 'pid': os.getpid(), 'routed_dcp': str(root / 'reports/faceDetect_amf_routed.dcp'), 'routing_errors': 0, 'note': 'Review timing, DRC, bus skew, and placement import diagnostics; no bitstream generated.'})
    except Exception as error:
        save_json(root / 'status.json', {'state': 'failed', 'stage': manifest['stages'][-1]['stage'] if manifest['stages'] else 'setup', 'pid': os.getpid(), 'updated': now(), 'error': str(error)})
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--run-dir', type=Path)
    parser.add_argument('--input-mode', choices=['export', 'benchmark'], default='export')
    parser.add_argument('--binary-dir', type=Path)
    args = parser.parse_args()
    if args.worker:
        worker(args.worker.resolve())
        return
    prefix = 'faceDetect-benchmark-' if args.input_mode == 'benchmark' else 'faceDetect-'
    root = args.run_dir or REPO / 'experiments/runs' / (prefix + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    root = root.resolve()
    prepare(root, args.input_mode, args.binary_dir)
    os.environ['AMF_PROJECT_ROOT'] = str(REPO)
    with (root / 'logs/runner.log').open('w') as log:
        process = subprocess.Popen([sys.executable, root / 'scripts/run_flow.py', '--worker', root], cwd=root, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (root / 'runner.pid').write_text(str(process.pid) + '\n')
    print(json.dumps({'run_directory': str(root), 'pid': process.pid, 'status_file': str(root / 'status.json')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
