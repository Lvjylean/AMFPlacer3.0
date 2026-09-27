"""Recorded full placement and routing. DCP artifacts remain server-side."""
import datetime as dt
import json
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


def run(root, args):
    from amf3 import save, stamp, git, machine
    root = Path(root)
    import_only = getattr(args, 'import_only', False)
    import_policy = 'repair' if getattr(args, 'allow_import_repair', False) else 'strict'
    if import_only and (args.amf_only or args.packing_only or import_policy != 'strict'):
        raise ValueError('--import-only requires strict acceptance and cannot combine with AMF-only modes')
    directory = root / 'experiments/runs' / ('getrf-u250-' + ('packing-' if args.packing_only else 'full-') + stamp())
    for name in ('inputs', 'reports', 'logs', 'placement', 'work'):
        (directory / name).mkdir(parents=True, exist_ok=False)
    shutil.copy2(Path(__file__), directory / 'inputs/run_full_flow.py')
    binary = (root / args.binary).resolve()
    config_path = root / args.placement_run / 'config.json' if args.placement_run else root / args.config
    config = json.loads(config_path.read_text())
    inputs = {}
    for key in ('vivado extracted device information file', 'vivado extracted design information file',
                'physical boundary model file', 'special pin offset info file', 'clock file', 'mergedSharedCellType2sharedCellType',
                'cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file',
                'fixed units file'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = dict(path=str(p), sha256=digest(p))
            config[key] = str(p)
    if config.get('PhysicalBoundaryMode') == 'true' or config.get('BoundaryAwareClustering') == 'true':
        if not config.get('physical boundary model file'):
            raise ValueError('Physical mode requires an explicit boundary model')
    if config.get('physical boundary model file'):
        from build_physical_boundaries import validate_model_inputs
        validate_model_inputs(config['physical boundary model file'], config['vivado extracted device information file'])
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
                    vivado_clock_source='original DCP constraints', placement_run=args.placement_run)
    manifest['import_policy'] = import_policy
    manifest['backend_mode'] = 'import-only' if import_only else 'full'
    build_manifest = binary.parent.parent / 'manifest.json'
    if build_manifest.is_file():
        shutil.copy2(build_manifest, directory / 'inputs/build_manifest.json')
        manifest['build_manifest_sha256'] = digest(build_manifest)
    (directory / 'inputs/working_tree.patch').write_bytes(subprocess.check_output(['git','diff','HEAD','--binary'],cwd=root))
    save(directory / 'manifest.json', manifest)
    print(directory, flush=True)

    def stage(name, command):
        save(directory / 'status.json', dict(state='running', stage=name))
        begin = time.monotonic()
        with (directory / 'logs' / (name + '.log')).open('w') as log:
            result = subprocess.run([str(v) for v in command], cwd=directory / 'work', stdout=log, stderr=subprocess.STDOUT)
        manifest['stages'].append(dict(name=name, command=[str(v) for v in command], elapsed_seconds=time.monotonic()-begin, exit_code=result.returncode))
        save(directory / 'manifest.json', manifest)
        if result.returncode:
            raise RuntimeError(name + ' failed; see ' + str(directory / 'logs' / (name + '.log')))

    try:
        if not args.placement_run:
            command = ['stdbuf','-oL','-eL',binary,directory/'config.json']
            if args.packing_only:
                command += ['--inspect-packing', directory/'reports/packing.tsv']
            stage('amf', command)
        if args.packing_only or args.amf_only:
            save(directory/'status.json',dict(state='completed', packing_only=args.packing_only,
                 full_placement_executed=not args.packing_only, routing_executed=False))
            return
        source = (root / args.placement_run).resolve() if args.placement_run else directory
        if args.placement_run and not completed_amf_placement(source):
            raise ValueError('Source AMF placement has not completed')
        from run_full_backend import prepare
        script = prepare(root, source, directory, manifest)
        save(directory/'manifest.json',manifest)
        stage('vivado', [machine()['vivado'],'-mode','batch','-notrace','-nojournal','-log',directory/'logs/vivado_internal.log',
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
