#!/usr/bin/env python3
"""Project entry point; run build/run/status on eda072."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def save(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    temporary.replace(path)


def stamp():
    return dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def machine():
    return json.loads((ROOT / 'configs/machines/eda072.json').read_text())


def require_server():
    if ROOT != Path(machine()['project_root']):
        raise RuntimeError('Run this command on eda072 in ' + machine()['project_root'])


def validate_run_id(value):
    import re
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value) or '..' in value:
        raise ValueError('Run ID must be a single safe directory name')
    return value


def build(args):
    require_server()
    commit = git('rev-parse', 'HEAD')
    build_id = 'build-' + stamp() + '-' + commit[:8]
    root = ROOT / 'builds' / build_id
    root.mkdir(parents=True, exist_ok=False)
    shutil.copytree(ROOT / 'src', root / 'src')
    (root / 'working_tree.patch').write_bytes(subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=ROOT))
    source_hashes = {str(p.relative_to(root / 'src')): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted((root / 'src').rglob('*')) if p.is_file()}
    save(root / 'source_hashes.json', source_hashes)
    record = {'build_id': build_id, 'source_commit': commit, 'git_status': git('status', '--porcelain'),
              'source_snapshot': str(root / 'src'), 'created': dt.datetime.now().astimezone().isoformat(),
              'jobs': args.jobs or machine()['build_jobs'], 'state': 'building', 'stages': [],
              'runtime_profiling': bool(args.profile)}
    record['tool_versions'] = {name: subprocess.check_output(command, text=True).strip()
                               for name, command in [('cmake', ['cmake', '--version']),
                                                     ('cxx', ['c++', '--version']),
                                                     ('ninja', ['ninja', '--version'])]}
    save(root / 'manifest.json', record)
    commands = [('configure', ['cmake', '-S', str(root / 'src'), '-B', str(root / 'build'), '-G', 'Ninja', '-DCMAKE_CXX_FLAGS=-g']),
                ('compile', ['cmake', '--build', str(root / 'build'), '--parallel', str(record['jobs']), '--target', 'AMFPlacer', 'partitionHyperGraph'])]
    if args.profile:
        commands[0][1].append('-DAMF_ENABLE_RUNTIME_PROFILING=ON')
    for stage, command in commands:
        print(stage + ': ' + str(root), flush=True)
        with (root / (stage + '.log')).open('w') as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
        record['stages'].append({'name': stage, 'command': command, 'exit_code': result.returncode})
        if result.returncode:
            record['state'] = 'failed'
            save(root / 'manifest.json', record)
            raise RuntimeError('Build failed; see ' + str(root / (stage + '.log')))
    record['state'] = 'completed'
    record['binaries'] = {name: hashlib.sha256((root / 'build' / name).read_bytes()).hexdigest()
                          for name in ['AMFPlacer', 'partitionHyperGraph']}
    save(root / 'manifest.json', record)
    if args.no_set_current:
        print(json.dumps({'build_id': build_id, 'binary_directory': str(root / 'build'), 'state': 'completed'}))
        return
    current = ROOT / 'builds/current'
    if current.exists() and not current.is_symlink():
        raise RuntimeError('Refusing to replace non-symlink builds/current')
    temporary = ROOT / 'builds' / ('current-' + stamp())
    temporary.symlink_to(root.relative_to(ROOT / 'builds') / 'build')
    temporary.replace(current)
    print(json.dumps({'build_id': build_id, 'binary_directory': str(current), 'state': 'completed'}))


def run(args):
    require_server()
    config_path = ROOT / args.config
    config = json.loads(config_path.read_text())
    settings = machine()
    binary_dir = ROOT / 'builds/current'
    dcp = ROOT / config['reference_dcp']
    for p in [binary_dir / 'AMFPlacer', binary_dir / 'partitionHyperGraph', dcp, Path(settings['vivado'])]:
        if not p.is_file():
            raise RuntimeError('Missing runtime input; build/configure first: ' + str(p))
    run_id = 'faceDetect-' + config['mode'] + '-' + stamp()
    directory = ROOT / 'experiments/runs' / run_id
    command = [sys.executable, str(ROOT / 'scripts/run_face_detect_flow.py'), '--input-mode', config['mode'],
               '--binary-dir', str(binary_dir.resolve()), '--run-dir', str(directory)]
    if args.dry_run:
        print(json.dumps({'command': command, 'dcp_location': str(dcp), 'dcp_storage': 'server-only'}, indent=2))
        return
    environment = dict(os.environ, AMF_PROJECT_ROOT=str(ROOT), AMF_REFERENCE_DCP=str(dcp),
                       AMF_VIVADO=settings['vivado'], AMF_JOBS=str(settings['amf_jobs']),
                       AMF_VIVADO_THREADS=str(settings['vivado_threads']))
    output = subprocess.check_output(command, env=environment, text=True)
    launched = json.loads(output)
    registry_path = ROOT / 'experiments/registry.json'
    registry = json.loads(registry_path.read_text())
    registry['runs'].append({'id': run_id, 'kind': 'full-' + config['mode'], 'experiment_config': str(config_path.relative_to(ROOT)), 'directory': 'experiments/runs/' + run_id,
                             'status_file': 'experiments/runs/' + run_id + '/status.json',
                             'build_directory': str(binary_dir.resolve()), 'created': dt.datetime.now().astimezone().isoformat()})
    save(registry_path, registry)
    print(json.dumps(launched, indent=2))


def inspect_inputs(args):
    require_server()
    from inspect_amf_inputs import inspect
    inspect(ROOT, args)


def legalize_resources(args):
    require_server()
    from inspect_amf_inputs import inspect
    inspect(ROOT, args, resources=True)


def status(args):
    require_server()
    if args.run:
        roots = [ROOT / 'experiments/runs' / validate_run_id(args.run)]
    else:
        roots = sorted((ROOT / 'experiments/runs').iterdir())
    for directory in roots:
        path = directory / 'status.json'
        if path.is_file():
            print(json.dumps({'run': directory.name, **json.loads(path.read_text())}, ensure_ascii=False))
        elif args.run:
            raise RuntimeError('No status file: ' + str(path))


def native_run(args):
    require_server()
    from run_native_face_detect import launch
    launch(ROOT, validate_run_id(args.reference_run), args.dry_run)


def validate_resources(args):
    require_server()
    from validate_resource_placement import validate
    validate(ROOT, args)


def validate_packing(args):
    require_server()
    from validate_srl_packing import validate
    validate(ROOT,args)


def full_run(args):
    require_server()
    from run_full_flow import run
    run(ROOT, args)


def compare_boundaries(args):
    require_server()
    from run_boundary_comparison import launch
    launch(ROOT,args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('compare-boundaries', help='Launch recorded three-arm GETRF physical-boundary comparison')
    p.add_argument('--binary', type=Path, default=Path('builds/current/AMFPlacer'))
    p.add_argument('--parallel', type=int, choices=(1,2,3), default=1)
    p.set_defaults(action=compare_boundaries)
    p = sub.add_parser('build', help='Create an isolated clean build')
    p.add_argument('--jobs', type=int)
    p.add_argument('--profile', action='store_true', help='Enable nested functional runtime profiling')
    p.add_argument('--no-set-current', action='store_true', help='Preserve the current default build')
    p.set_defaults(action=build)
    p = sub.add_parser('run', help='Launch a configured faceDetect flow')
    p.add_argument('--config', type=Path, default=Path('configs/experiments/faceDetect-baseline.json'))
    p.add_argument('--dry-run', action='store_true')
    p.set_defaults(action=run)
    p = sub.add_parser('native-run', help='Run a native Vivado control from a completed AMF experiment')
    p.add_argument('--reference-run', required=True)
    p.add_argument('--dry-run', action='store_true')
    p.set_defaults(action=native_run)
    p = sub.add_parser('inspect', help='Load and report device/netlist inputs without placement')
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--binary', type=Path, default=Path('builds/current/AMFPlacer'))
    p.set_defaults(action=inspect_inputs)
    p = sub.add_parser('legalize-resources', help='Legalize URAM/DSP/BRAM/Carry without full CLB placement')
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--binary', type=Path, default=Path('builds/current/AMFPlacer'))
    p.set_defaults(action=legalize_resources)
    p = sub.add_parser('validate-resources', help='Vivado audit of partial resource placement')
    p.add_argument('--resource-run', required=True)
    p.add_argument('--dcp', default='data/reference/getrf-u250/post_opt.dcp')
    p.set_defaults(action=validate_resources)
    p = sub.add_parser('full-run', help='Recorded AMF full placement and Vivado routing')
    p.add_argument('--profile', action='store_true', help='Collect runtime scopes from a profiling build')
    p.add_argument('--config', default='configs/experiments/getrf-u250-full.json')
    p.add_argument('--binary', default='builds/current/AMFPlacer')
    p.add_argument('--dcp', default='data/reference/getrf-u250/post_opt.dcp')
    p.add_argument('--packing-only', action='store_true')
    p.add_argument('--amf-only', action='store_true')
    p.add_argument('--import-only', action='store_true', help='Stop after strict Vivado import acceptance')
    p.add_argument('--allow-import-repair', action='store_true', help='Diagnostic legacy policy: allow rejected imports to reach place_design')
    p.add_argument('--placement-run', help='Completed AMF run to import and route in a new experiment')
    p.set_defaults(action=full_run)
    p = sub.add_parser('validate-packing', help='Vivado audit of SRL/MUX BEL maps')
    p.add_argument('--packing-run', required=True)
    p.add_argument('--dcp', default='data/reference/getrf-u250/post_opt.dcp')
    p.set_defaults(action=validate_packing)
    p = sub.add_parser('status', help='Read experiment status')
    p.add_argument('--run')
    p.set_defaults(action=status)
    args = parser.parse_args()
    if getattr(args, 'jobs', None) is not None and args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        args.action(args)
    except (RuntimeError, ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
