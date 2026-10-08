"""Recorded full placement and routing. DCP artifacts remain server-side."""
import datetime as dt
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import time
from inspect_amf_inputs import digest


def configure_outputs(config, directory):
    # simpleJSON prefixes dumpDirectory to every Dump* value, including absolute paths.
    config['dumpDirectory'] = str(directory / 'placement')
    config['DumpCLBPacking'] = 'DumpCLBPacking'
    if config.get('physical boundary model file'):
        config['BoundaryReportDirectory'] = str(directory / 'reports/physical')


def completed_amf_placement(source):
    """A later backend failure must not discard an already completed AMF placement."""
    source = Path(source)
    if not (source/'placement/DumpCLBPacking-first-0.tcl').is_file():
        return False
    manifest = json.loads((source/'manifest.json').read_text())
    return any(s.get('name') == 'amf' and s.get('exit_code') == 0 for s in manifest['stages'])


def resolve_import_policy(args):
    strict = getattr(args, 'strict_import', False)
    repair = getattr(args, 'allow_import_repair', False)
    import_only = getattr(args, 'import_only', False)
    if repair and (strict or import_only):
        raise ValueError('--allow-import-repair cannot combine with --strict-import or --import-only')
    return 'strict' if strict or import_only else 'repair'


def global_inspection_requested(args):
    enabled = bool(getattr(args, 'global_only', False))
    if enabled and any(getattr(args, k, False) for k in ('amf_only', 'packing_only', 'import_only', 'placement_run')):
        raise ValueError('--global-only cannot combine with AMF/packing/import-only or placement reuse')
    return enabled


def run(root, args):
    from amf3 import save, stamp, git, machine
    root = Path(root)
    global_only = global_inspection_requested(args)
    import_only = getattr(args, 'import_only', False)
    profiling = getattr(args, 'profile', False)
    if profiling and args.placement_run:
        raise ValueError('--profile requires a new AMF execution')
    import_policy = resolve_import_policy(args)
    upstream_backend = getattr(args, 'upstream_backend', False)
    if upstream_backend and (import_policy != 'repair' or profiling or getattr(args, 'release_fixed_clock_buffers', False)):
        raise ValueError('Upstream backend requires original repair policy, without profiling or clock release')
    if import_only and (args.amf_only or args.packing_only or import_policy != 'strict'):
        raise ValueError('--import-only requires strict acceptance and cannot combine with AMF-only modes')
    vivado = str((root / (getattr(args, 'vivado', None) or machine()['vivado'])).resolve())
    vivado_version = None
    if not global_only and not args.amf_only and not args.packing_only:
        vivado_version = subprocess.check_output([vivado, '-version'], text=True, stderr=subprocess.STDOUT, timeout=60).strip()
    prefix = getattr(args, 'run_prefix', 'getrf-u250')
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]*', prefix):
        raise ValueError('Invalid run prefix')
    directory = root / 'experiments/runs' / (prefix + '-' + ('global-' if global_only else 'packing-' if args.packing_only else 'full-') + stamp())
    for name in ('inputs', 'reports', 'logs', 'placement', 'work'):
        (directory / name).mkdir(parents=True, exist_ok=False)
    shutil.copy2(Path(__file__), directory / 'inputs/run_full_flow.py')
    binary = (root / args.binary).resolve()
    if global_only:
        capabilities = json.loads(subprocess.check_output([str(binary), '--capabilities'], text=True, timeout=15))
        if capabilities.get('global_placement_inspection_schema') != 1:
            raise ValueError('Binary lacks global-placement inspection; refusing a full placement fallback')
    config_path = root / args.placement_run / 'config.json' if args.placement_run else root / args.config
    config = json.loads(config_path.read_text())
    inputs = {}
    for key in ('vivado extracted device information file', 'vivado extracted design information file',
                'physical boundary model file', 'special pin offset info file', 'clock file', 'mergedSharedCellType2sharedCellType',
                'cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file',
                'fixed units file', 'unpredictable macro file', 'designCluster', 'DSP registered outputs file',
                'clock resource capacity file', 'external floorplan membership file',
                'external floorplan regions file', 'external floorplan manifest file'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = dict(path=str(p), sha256=digest(p))
            config[key] = str(p)
    from paper_boundary_config import validate_paper_boundary_binary
    validate_paper_boundary_binary(config, binary)
    from import_external_floorplan import validate_external_inputs
    validate_external_inputs(config, binary)
    if config.get('clock resource capacity file'):
        from clock_resource_capacity import validate_capacity_inputs, validate_capacity_binary
        validate_capacity_inputs(config['clock resource capacity file'],
                                 config['vivado extracted device information file'],
                                 config.get('clock resource capacity part'))
        validate_capacity_binary(binary)
    if config.get('PhysicalBoundaryMode') == 'true' or config.get('BoundaryAwareClustering') == 'true':
        if not config.get('physical boundary model file'):
            raise ValueError('Physical mode requires an explicit boundary model')
    if config.get('physical boundary model file'):
        from build_physical_boundaries import validate_model_inputs
        validate_model_inputs(config['physical boundary model file'], config['vivado extracted device information file'])
    if config.get('external floorplan membership file'):
        config['external floorplan report file'] = str(directory / 'reports/external_floorplan.json')
    configure_outputs(config, directory)
    if config.get('physical boundary model file'):
        physical = directory/'reports/physical'
        physical.mkdir()
        model_dir = Path(config['physical boundary model file']).parent
        for name in ('boundaries.json','physical_regions.svg'):
            shutil.copy2(model_dir/name,physical/name)
        save(physical/'effective_parameters.json', {k:v for k,v in config.items() if k.startswith(('Boundary','Physical','SLR','TimingMax','QP')) or k in ('ClockPeriod','physical device part','physical boundary model file')})
    save(directory / 'config.json', config)
    dcp = (root / args.dcp).resolve()
    manifest = dict(schema='amf-full-flow-v1', source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), inputs=inputs, binary=str(binary),
                    binary_sha256=digest(binary), input_dcp=str(dcp), input_dcp_sha256=digest(dcp),
                    config_sha256=digest(directory / 'config.json'), dcp_storage='server-only',
                    started=dt.datetime.now().astimezone().isoformat(), stages=[],
                    amf_clock_period_ns=config['ClockPeriod'], amf_clock_source='explicit experiment configuration',
                    vivado_clock_source='not executed' if global_only else 'original DCP constraints', placement_run=args.placement_run)
    manifest['vivado_executable'] = vivado
    manifest['vivado_version_output'] = vivado_version
    manifest['import_policy'] = import_policy
    manifest['upstream_backend'] = upstream_backend
    manifest['runtime_profiling'] = profiling
    manifest['backend_mode'] = 'global-only' if global_only else 'import-only' if import_only else 'full'
    manifest['release_fixed_clock_buffers'] = bool(getattr(args, 'release_fixed_clock_buffers', False))
    provenance = getattr(args, 'input_provenance', None)
    if provenance:
        provenance = (root / provenance).resolve()
        record = json.loads(provenance.read_text())
        if record['input_dcp_sha256'] != manifest['input_dcp_sha256']:
            raise ValueError('Input provenance belongs to a different DCP')
        shutil.copy2(provenance, directory / 'inputs/input_provenance.json')
        manifest['input_provenance'] = record
        manifest['input_provenance_sha256'] = digest(provenance)
        manifest['vivado_clock_source'] = record['vivado_clock_source']
    build_manifest = binary.parent.parent / 'manifest.json'
    if build_manifest.is_file():
        build_record = json.loads(build_manifest.read_text())
        manifest['algorithm_build_manifest'] = str(build_manifest)
        manifest['algorithm_source_commit'] = build_record.get('source_commit') or build_record.get('source', {}).get('commit')
        if profiling and not json.loads(build_manifest.read_text()).get('runtime_profiling'):
            raise ValueError('--profile requires a build created with amf3.py build --profile')
        shutil.copy2(build_manifest, directory / 'inputs/build_manifest.json')
        manifest['build_manifest_sha256'] = digest(build_manifest)
    if upstream_backend:
        if not build_manifest.is_file() or build_record.get('source_modified') is not False or build_record.get('binaries', {}).get('AMFPlacer') != manifest['binary_sha256']:
            raise ValueError('Upstream backend requires a verified unmodified upstream build')
        if config.get('physical boundary model file'):
            raise ValueError('Original upstream flow does not use the AMF3 physical boundary model')
    (directory / 'inputs/working_tree.patch').write_bytes(subprocess.check_output(['git','diff','HEAD','--binary'],cwd=root))
    save(directory / 'manifest.json', manifest)
    print(directory, flush=True)

    def stage(name, command):
        save(directory / 'status.json', dict(state='running', stage=name))
        begin = time.monotonic()
        with (directory / 'logs' / (name + '.log')).open('w') as log:
            environment = dict(os.environ)
            if profiling and name == 'amf':
                environment['AMF_PROFILE_OUTPUT'] = str(directory/'reports/amf_profile.tsv')
            else:
                environment.pop('AMF_PROFILE_OUTPUT', None)
            result = subprocess.run([str(v) for v in command], cwd=directory / 'work', stdout=log, stderr=subprocess.STDOUT, env=environment)
        manifest['stages'].append(dict(name=name, command=[str(v) for v in command], elapsed_seconds=time.monotonic()-begin, exit_code=result.returncode))
        save(directory / 'manifest.json', manifest)
        if result.returncode:
            raise RuntimeError(name + ' failed; see ' + str(directory / 'logs' / (name + '.log')))

    try:
        if not args.placement_run:
            command = ['stdbuf','-oL','-eL',binary,directory/'config.json']
            if global_only:
                command += ['--inspect-global-placement', directory/'reports/global_placement.json']
            if args.packing_only:
                command += ['--inspect-packing', directory/'reports/packing.tsv']
            stage('amf', command)
            if profiling:
                metadata = json.loads((directory/'reports/amf_profile.tsv.meta.json').read_text())
                if not metadata.get('complete'):
                    raise RuntimeError('Runtime profile did not close all scopes')
        if global_only:
            report = directory/'reports/global_placement.json'
            inspected = json.loads(report.read_text())
            if (inspected.get('schema') != 'amf-global-placement-inspection-v1' or
                    not inspected.get('global_placement_executed') or inspected.get('final_packing_executed') or
                    inspected.get('routing_executed') or not Path(str(report)+'.cells.tsv.gz').is_file()):
                raise ValueError('Incomplete or inconsistent global-placement inspection output')
            save(directory/'status.json', dict(state='completed', global_placement_executed=True,
                 full_placement_executed=False, final_packing_executed=False, routing_executed=False,
                 finished=dt.datetime.now().astimezone().isoformat()))
            return
        if args.packing_only or args.amf_only:
            save(directory/'status.json',dict(state='completed', packing_only=args.packing_only,
                 full_placement_executed=not args.packing_only, routing_executed=False,
                 runtime_profiling=profiling, finished=dt.datetime.now().astimezone().isoformat()))
            return
        source = (root / args.placement_run).resolve() if args.placement_run else directory
        if args.placement_run and not completed_amf_placement(source):
            raise ValueError('Source AMF placement has not completed')
        from run_full_backend import prepare
        save(directory/'status.json', dict(state='running', stage='amf_to_vivado_adapter'))
        adapter_begin = time.monotonic()
        script = prepare(root, source, directory, manifest)
        manifest['stages'].append(dict(name='amf_to_vivado_adapter', function='run_full_backend.prepare',
            elapsed_seconds=time.monotonic()-adapter_begin, exit_code=0))
        save(directory/'manifest.json',manifest)
        stage('vivado', [vivado,'-mode','batch','-notrace','-nojournal','-log',directory/'logs/vivado_internal.log',
                        '-source',script,'-tclargs',dcp,directory/'reports',directory/'placement',
                        import_policy, manifest['backend_mode']])
        if import_only:
            save(directory/'status.json',dict(state='completed',strict_import_verified=True,
                 full_placement_executed=False,routing_executed=False,
                 finished=dt.datetime.now().astimezone().isoformat()))
            return
        if config.get('physical boundary model file'):
            from diagnostics.analyze_boundary_timing_samples import analyze, audit_placements
            model = config['physical boundary model file']
            analyze(model, directory/'reports/physical', float(config.get('SLRBoundaryDelayNs', '1.5')))
            audit_placements(directory, model, config['vivado extracted design information file'], config.get('clock file'))
        from summarize_full_flow import summarize
        summary = summarize(directory)
        save(directory/'reports/summary.json',summary)
        output = directory/'reports/getrf_routed.dcp'
        save(directory/'status.json',dict(state='completed',full_placement_executed=True,routing_executed=True,
             routing_complete=summary['routing_complete'],drc_errors=summary['drc_errors'],timing_met=summary['timing_met'],
             amf_export_complete=summary['amf_export_complete'],implementation_verified=summary['implementation_verified'],
             strict_import_verified=summary['strict_import_verified'],
             output_dcp_sha256=digest(output),finished=dt.datetime.now().astimezone().isoformat()))
    except Exception as error:
        save(directory/'status.json',dict(state='failed',error=str(error),finished=dt.datetime.now().astimezone().isoformat()))
        raise
