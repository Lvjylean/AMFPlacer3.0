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
    directory = root / 'experiments/runs' / ('getrf-u250-' + ('packing-' if args.packing_only else 'full-') + stamp())
    for name in ('inputs', 'reports', 'logs', 'placement', 'work'):
        (directory / name).mkdir(parents=True, exist_ok=False)
    shutil.copy2(Path(__file__), directory / 'inputs/run_full_flow.py')
    binary = (root / args.binary).resolve()
    config_path = root / args.placement_run / 'config.json' if args.placement_run else root / args.config
    config = json.loads(config_path.read_text())
    inputs = {}
    for key in ('vivado extracted device information file', 'vivado extracted design information file',
                'special pin offset info file', 'clock file', 'mergedSharedCellType2sharedCellType',
                'cellType2fixedAmo file', 'cellType2sharedCellType file', 'sharedCellType2BELtype file',
                'fixed units file'):
        if config.get(key):
            p = (root / config[key]).resolve()
            inputs[key] = dict(path=str(p), sha256=digest(p))
            config[key] = str(p)
    configure_outputs(config, directory)
    save(directory / 'config.json', config)
    dcp = (root / args.dcp).resolve()
    manifest = dict(schema='amf-full-flow-v1', source_commit=git('rev-parse', 'HEAD'),
                    git_status=git('status', '--porcelain'), inputs=inputs, binary=str(binary),
                    binary_sha256=digest(binary), input_dcp=str(dcp), input_dcp_sha256=digest(dcp),
                    config_sha256=digest(directory / 'config.json'), dcp_storage='server-only',
                    started=dt.datetime.now().astimezone().isoformat(), stages=[],
                    amf_clock_period_ns=config['ClockPeriod'], amf_clock_source='explicit experiment configuration',
                    vivado_clock_source='original DCP constraints', placement_run=args.placement_run)
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
                        '-source',script,'-tclargs',dcp,directory/'reports',directory/'placement'])
        from summarize_full_flow import summarize
        summary = summarize(directory)
        save(directory/'reports/summary.json',summary)
        output = directory/'reports/getrf_routed.dcp'
        save(directory/'status.json',dict(state='completed',full_placement_executed=True,routing_executed=True,
             routing_complete=summary['routing_complete'],drc_errors=summary['drc_errors'],timing_met=summary['timing_met'],
             amf_export_complete=summary['amf_export_complete'],implementation_verified=summary['implementation_verified'],
             output_dcp_sha256=digest(output),finished=dt.datetime.now().astimezone().isoformat()))
    except Exception as error:
        save(directory/'status.json',dict(state='failed',error=str(error),finished=dt.datetime.now().astimezone().isoformat()))
        raise
