"""Record native Vivado placement/routing against a full-run input checkpoint."""
import datetime as dt
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time

from summarize_face_detect_flow import timing, drc
from summarize_full_flow import routing_complete


def constraint_options(args, provenance):
    preserve = bool(args.preserve_input_constraints)
    override = bool(getattr(args, 'override_core_clock', False))
    if override and not preserve:
        raise ValueError('Core clock override requires --preserve-input-constraints')
    if preserve:
        if args.release_io or not args.core_clock or args.clock_period is None:
            raise ValueError('Preserving input constraints requires a core clock/period and forbids releasing I/O')
        if not math.isfinite(args.clock_period) or args.clock_period <= 0:
            raise ValueError('Core period must be positive and finite')
        return dict(preserve_input_constraints=True, core_clock=args.core_clock,
                    clock_count=0, period_ns=args.clock_period, override_core_clock=override)
    if args.core_clock or args.clock_period is not None:
        raise ValueError('Core clock validation requires --preserve-input-constraints')
    return dict(preserve_input_constraints=False, core_clock='',
                clock_count=provenance['clock_count'], period_ns=provenance['vivado_clock_period_ns'])


def probe_vivado(root, executable):
    path = (Path(root) / executable).resolve()
    if not path.is_file():
        raise ValueError('Vivado executable not found: ' + str(path))
    probe = subprocess.run([str(path), '-version'], capture_output=True, text=True,
                           check=True, timeout=120)
    output = probe.stdout + probe.stderr
    match = re.search(r'\b[Vv]ivado v(\d+\.\d+)\b', output)
    if not match:
        raise ValueError('Cannot identify selected Vivado version')
    return str(path), match.group(1), output


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            h.update(block)
    return h.hexdigest()


def collect(directory):
    reports = directory / 'reports'
    manifest = json.loads((directory / 'manifest.json').read_text())
    result = dict(case=manifest['case'], reference_run=manifest['reference_run'],
                  input_dcp_sha256=manifest['input_dcp_sha256'],
                  same_reference_input_dcp=True, implementation_verified=False,
                  routing_complete=False, timing_met=None, vivado_stages=[])
    stages = reports / 'stages.tsv'
    if stages.exists():
        for line in stages.read_text().splitlines()[1:]:
            name, elapsed, code = line.split('\t')
            result['vivado_stages'].append(dict(name=name, elapsed_seconds=float(elapsed), tcl_status=int(code)))
    if (reports / 'route_status.rpt').exists():
        counts = {}
        for line in (reports / 'route_status.rpt').read_text().splitlines():
            match = re.search(r'# of\s+(.+?)\s*:\s*(\d+)', line)
            if match:
                counts[match[1].rstrip('. ')] = int(match[2])
        result['routing_counts'] = counts
        result['routing_complete'] = routing_complete(counts)
    if (reports / 'timing_summary.rpt').exists():
        result['timing'] = timing(reports / 'timing_summary.rpt')
        result['timing_met'] = all(result['timing'][k] >= 0 for k in ('wns_ns', 'whs_ns', 'wpws_ns'))
    if (reports / 'drc_counts.json').exists():
        result['drc_counts'] = json.loads((reports / 'drc_counts.json').read_text())
        result['drc'] = drc(reports / 'drc.rpt')
        assert result['drc']['totals'].get('Error', 0) == result['drc_counts']['errors']
        result['routing_drc_verified'] = result['routing_complete'] and result['drc_counts']['errors'] == 0
    for name in ('release_placement.json', 'tools.json', 'constraint_audit.json', 'core_clock_override.json'):
        if (reports / name).exists():
            result[name[:-5]] = json.loads((reports / name).read_text())
    checkpoint = reports / 'vivado_routed.dcp'
    if checkpoint.exists():
        result['final_dcp'] = str(checkpoint)
        result['final_dcp_sha256'] = digest(checkpoint)
    audit = result.get('constraint_audit', {})
    result['implementation_verified'] = bool(result.get('routing_drc_verified') and
        audit.get('clock_snapshot_unchanged') and audit.get('io_standards_unchanged') and
        (not manifest['config'].get('preserve_input_constraints') or audit.get('input_fixed_locations_verified')) and
        result.get('final_dcp_sha256'))
    if manifest['config'].get('override_core_clock'):
        override = result.get('core_clock_override', {})
        result['core_clock_override_verified'] = bool(override.get('applied') and
            override.get('other_clocks_unchanged') and
            override.get('core_clock') == manifest['config']['core_clock'] and
            abs(override.get('target_period_ns', -1) - manifest['config']['period_ns']) < 1e-6)
        result['implementation_verified'] = bool(result['implementation_verified'] and
            result['core_clock_override_verified'])
    return result


def run(root, args):
    from amf3 import save, stamp, git, machine, validate_run_id
    root = Path(root)
    reference = root / 'experiments/runs' / validate_run_id(args.reference_run)
    old = json.loads((reference / 'manifest.json').read_text())
    source = Path(old['input_dcp'])
    if digest(source) != old['input_dcp_sha256']:
        raise ValueError('Reference input DCP hash changed')
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]*', args.run_prefix):
        raise ValueError('Invalid run prefix')
    provenance = old['input_provenance']
    tools = json.loads((reference / 'reports/tools.json').read_text())
    vivado, vivado_version, vivado_version_output = probe_vivado(
        root, getattr(args, 'vivado', None) or machine()['vivado'])
    constraints = constraint_options(args, provenance)
    resume = None
    execution_source = source
    if args.resume_native_run:
        if constraints.get('override_core_clock'):
            raise ValueError('Recovery must reuse its already-applied clock target without overriding it again')
        if not constraints['preserve_input_constraints'] or args.opt_design:
            raise ValueError('Native recovery preserves input constraints and cannot add logic optimization')
        resume = root / 'experiments/runs' / validate_run_id(args.resume_native_run)
        previous = json.loads((resume / 'manifest.json').read_text())
        if previous['input_dcp_sha256'] != old['input_dcp_sha256'] or previous['reference_run'] != str(reference):
            raise ValueError('Recovery does not share the reference input')
        if previous['config']['core_clock'] != constraints['core_clock'] or previous['config']['period_ns'] != constraints['period_ns']:
            raise ValueError('Recovery clock target changed')
        if not previous['config']['preserve_input_constraints']:
            raise ValueError('Cannot recover a native run with different constraints')
        execution_source = resume / 'reports/vivado_placed.dcp'
        if not execution_source.is_file():
            raise ValueError('Recovery placed checkpoint is missing')
    if args.physical_audit and 'physical boundary model file' not in old.get('inputs', {}):
        raise ValueError('Reference run has no physical boundary model')
    directory = root / 'experiments/runs' / (args.run_prefix + '-' + stamp())
    for name in ('inputs', 'reports', 'logs', 'work'):
        (directory / name).mkdir(parents=True, exist_ok=False)
    config = dict(case=provenance.get('case', args.run_prefix), part=tools['part'], **constraints,
                  vivado_version=vivado_version, reference_vivado_version=tools['vivado_version'], vivado_threads=4,
                  opt_design=bool(args.opt_design), place_directive='Default', route_directive='Default',
                  io_package_pins='Vivado automatic reassignment' if args.release_io else 'preserved from reference DCP',
                  release_io=bool(args.release_io),
                  cell_placement=('direct native placement; board/IP constraints preserved; no AMF placement imported'
                                  if constraints['preserve_input_constraints'] else 'unfixed and unplaced; BUFG locations released'),
                  physical_audit=bool(args.physical_audit),
                  resume_native_run=str(resume) if resume else None,
                  dcp_storage='server-only', amf_executed=False)
    save(directory / 'config.json', config)
    source_hashes = {}
    for name in ('amf3.py', 'run_vivado_baseline.py', 'vivado_baseline.tcl',
                 'summarize_full_flow.py', 'summarize_face_detect_flow.py'):
        shutil.copy2(root / 'scripts' / name, directory / 'inputs' / name)
        source_hashes[name] = digest(directory / 'inputs' / name)
    if args.physical_audit:
        for name in ('export_boundary_timing_samples.tcl', 'analyze_boundary_timing_samples.py'):
            shutil.copy2(root / 'scripts/diagnostics' / name, directory / 'inputs' / name)
            source_hashes[name] = digest(directory / 'inputs' / name)
    save(directory / 'inputs/reference_manifest.json', old)
    save(directory / 'inputs/input_provenance.json', provenance)
    mismatch_file = reference / 'reports/routed_placement_mismatches.tsv'
    with (directory / 'inputs/reference_fixed_relocations.tsv').open('w') as output:
        writer = csv.writer(output, delimiter='\t')
        writer.writerow(['cell', 'requested_site', 'requested_bel', 'actual_site', 'actual_bel'])
        if mismatch_file.exists():
            with mismatch_file.open() as stream:
                for row in csv.DictReader(stream, delimiter='\t'):
                    site, _, bel = row['requested'].partition('/')
                    if site.startswith('BUFG') and row['actual_site'].startswith('BUFG'):
                        writer.writerow([row['cell'], site, bel, row['actual_site'], row['actual_bel']])
    source_hashes['reference_fixed_relocations.tsv'] = digest(directory / 'inputs/reference_fixed_relocations.tsv')
    if resume:
        for name in ('input_fixed_cells.tsv', 'input_ports.tsv', 'clocks.rpt'):
            shutil.copy2(resume / 'reports' / name, directory / 'inputs' / ('original_' + name))
            source_hashes['original_' + name] = digest(directory / 'inputs' / ('original_' + name))
        pattern = re.compile(r'^(\S+)\s+(\d+\.\d+)\s+\{([^}]+)\}\s+[A-Z,]+\s+\{(.+)\}$', re.M)
        clocks = pattern.findall((resume / 'reports/clocks.rpt').read_text())
        if not clocks:
            raise ValueError('No original clocks parsed for recovery')
        with (directory / 'inputs/original_clocks.tsv').open('w') as output:
            writer = csv.writer(output, delimiter='\t');writer.writerow(['clock','period','waveform','source'])
            writer.writerows(clocks)
        source_hashes['original_clocks.tsv'] = digest(directory / 'inputs/original_clocks.tsv')
    (directory / 'inputs/working_tree.patch').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=root))
    (directory / 'inputs/server_load.txt').write_text(subprocess.check_output(['uptime'], text=True))
    command = [vivado, '-mode', 'batch', '-notrace', '-nojournal',
               '-log', str(directory / 'logs/vivado_internal.log'), '-source',
               str(directory / 'inputs/vivado_baseline.tcl'), '-tclargs', str(execution_source),
               str(directory / 'reports'), config['part'], str(config['clock_count']),
               str(config['period_ns']), str(int(config['opt_design'])), str(int(config['release_io'])),
               str(int(config['preserve_input_constraints'])), config['core_clock'], config['vivado_version'],
               str(int(config['physical_audit'])), str(int(resume is not None)),
               str(int(config.get('override_core_clock', False)))]
    manifest = dict(schema='vivado-native-baseline-v1', case=config['case'], reference_run=str(reference),
                    input_dcp=str(source), input_dcp_sha256=digest(source), source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), script_hashes=source_hashes,
                    started=dt.datetime.now().astimezone().isoformat(), config=config,
                    build_source='Vivado installed executable; no AMF binary used', command=command,
                    dcp_storage='server-only', stages=[])
    manifest['vivado_executable'] = vivado
    manifest['vivado_version_output'] = vivado_version_output
    manifest['reference_manifest_sha256'] = digest(reference / 'manifest.json')
    manifest['reference_input_provenance'] = provenance
    manifest['execution_checkpoint'] = dict(path=str(execution_source), sha256=digest(execution_source))
    if resume:
        manifest['recovery'] = dict(source_run=str(resume), source_manifest_sha256=digest(resume / 'manifest.json'),
                                   mode='reuse completed native placement; rerun routing and finish reports/checkpoint',
                                   placement_runtime_reused=True)
    if args.physical_audit:
        manifest['physical_boundary_model'] = old['inputs']['physical boundary model file']
    save(directory / 'manifest.json', manifest)
    save(directory / 'status.json', dict(state='running', stage='vivado'))
    print(directory, flush=True)
    begin = time.monotonic()
    try:
        with (directory / 'logs/vivado.log').open('w') as log:
            process = subprocess.Popen(command, cwd=directory / 'work', stdout=log, stderr=subprocess.STDOUT)
            save(directory / 'status.json', dict(state='running', stage='vivado', vivado_launcher_pid=process.pid))
            code = process.wait()
        manifest['stages'].append(dict(name='vivado', elapsed_seconds=time.monotonic()-begin, exit_code=code))
        save(directory / 'manifest.json', manifest)
        if args.physical_audit and (directory / 'reports/physical/timing_paths.tsv').exists():
            import importlib.util
            spec = importlib.util.spec_from_file_location('frozen_boundary_analysis', directory / 'inputs/analyze_boundary_timing_samples.py')
            analysis = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(analysis)
            model = old['inputs']['physical boundary model file']
            if digest(model['path']) != model['sha256']:
                raise ValueError('Reference physical model hash changed')
            analysis.analyze(Path(model['path']), directory / 'reports/physical')
        summary = collect(directory)
        save(directory / 'reports/summary.json', summary)
        if code:
            raise RuntimeError('Vivado failed; see logs/vivado.log')
        if 'final_dcp_sha256' not in summary:
            raise RuntimeError('Vivado returned without a final DCP')
        save(directory / 'status.json', dict(state='completed', finished=dt.datetime.now().astimezone().isoformat(),
             implementation_verified=summary['implementation_verified'], routing_complete=summary['routing_complete'],
             timing_met=summary['timing_met'], output_dcp_sha256=summary['final_dcp_sha256']))
    except Exception as error:
        save(directory / 'status.json', dict(state='failed', error=str(error),
             finished=dt.datetime.now().astimezone().isoformat()))
        raise
