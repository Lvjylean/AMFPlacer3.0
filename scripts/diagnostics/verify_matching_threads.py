#!/usr/bin/env python3
"""Run sequential 12/16-worker GETRF AMF comparisons; never invoke Vivado.

Use a frozen binary and the same input configuration. Output paths differ, but
jobs is the only algorithm setting changed within a pair. AMF_MACHINE_CONFIG
may select a host-specific machine file. Evidence is retained on failure.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import traceback


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def write(path, value):
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
    temp.replace(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--dcp', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--quantity', choices=('off', 'on'), required=True)
    a = p.parse_args()
    repo, binary, output = a.repo.resolve(), a.binary.resolve(), a.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    state = dict(state='running', host=os.uname().nodename, pid=os.getpid(),
                 started=datetime.datetime.now().astimezone().isoformat(),
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
                 binary=str(binary), binary_sha256=sha(binary),
                 input_dcp=str(a.dcp.resolve()), input_dcp_sha256=sha(a.dcp),
                 quantity=a.quantity, order=[12, 16], stages=[])
    status = output / 'status.json'
    write(status, state)
    try:
        cfg = json.loads(a.config.read_text())
        assert cfg['ProPrimitiveTiming'] == 'false'
        assert cfg['BipartiteMatchingBackend'] == 'legacy_components'
        cfg.update(SllQuantityDiagnostics='true', SllQuantityEnable=str(a.quantity == 'on').lower())
        assert float(cfg['SllQuantityWeight']) > 0
        assert cfg['SllRegionPolicy'] == 'preserve'
        configs, matches, artifacts = [], [], []
        for workers in (12, 16):
            label = f'{a.quantity}-threads{workers}'
            cfg['jobs'] = str(workers)
            cfg['SllReportDirectory'] = str(output / label / 'sll')
            config = output / f'{label}.json'
            write(config, cfg)
            configs.append(dict(cfg))
            env = {k: v for k, v in os.environ.items() if not k.startswith('AMF_') or k == 'AMF_MACHINE_CONFIG'}
            env.update(AMF_MACRO_STUDY_MODE='control', AMF_MACRO_STUDY_START='1',
                       OMP_NUM_THREADS=str(workers))
            command = ['python3', 'scripts/amf3.py', 'full-run', '--amf-only', '--profile',
                       '--config', str(config), '--binary', str(binary), '--dcp', str(a.dcp.resolve()),
                       '--run-prefix', f'getrf-sll-components-{label}']
            stage = dict(threads=workers, state='running', command=command,
                         config_sha256=sha(config), environment={k: v for k, v in env.items()
                         if k.startswith(('AMF_', 'OMP_', 'MKL_'))})
            state['stages'].append(stage)
            logpath = output / f'{label}.log'
            pattern = f'getrf-sll-components-{label}-full-*'
            previous_runs = set((repo / 'experiments/runs').glob(pattern))
            start = time.monotonic()
            with logpath.open('w') as log:
                child = subprocess.Popen(command, cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT)
                stage['pid'] = child.pid
                write(status, state)
                code = child.wait()
            stage.update(state='completed' if code == 0 else 'failed', exit_code=code,
                         launcher_wall_seconds=time.monotonic() - start)
            runs = sorted(set((repo / 'experiments/runs').glob(pattern)) - previous_runs)
            assert len(runs) == 1, f'Ambiguous run directory: {runs}'
            run = runs[0]
            stage['run_directory'] = str(run)
            write(status, state)
            assert code == 0, f'{label} failed; inspect {logpath}'
            manifest = json.loads((run / 'manifest.json').read_text())
            assert manifest['binary_sha256'] == state['binary_sha256']
            assert not any('vivado' in s['name'].lower() for s in manifest['stages'])
            stage['amf_process_seconds'] = next(s['elapsed_seconds'] for s in manifest['stages'] if s['name'] == 'amf')
            stage['input_hashes'] = {k: v['sha256'] for k, v in manifest['inputs'].items()}
            matching = []
            for log in sorted((run / 'logs').glob('*')):
                if not log.is_file():
                    continue
                for line in log.read_text(errors='replace').splitlines():
                    if 'AMF_MATCHER backend=legacy_components ' in line:
                        record = dict(re.findall(r'(\w+)=([^\s]+)', line))
                        matching.append({k: v for k, v in record.items()
                                         if k != 'threads' and not k.endswith('_s')})
                    if 'AMF_MATCHER backend=legacy ' in line:
                        raise AssertionError('Unexpected legacy grouped matcher')
            assert matching, 'Missing component matching evidence'
            write(output / f'{label}-matching.json', matching)
            matches.append(matching)
            paths = {'placement_tcl': run / 'placement/DumpCLBPacking-first-0.tcl'}
            paths.update({name: Path(cfg['SllReportDirectory']) / name for name in ('stages.tsv', 'qp.tsv', 'mapping.tsv')})
            digests = {name: sha(path) for name, path in paths.items()}
            if a.quantity == 'on':
                assert len(paths['qp.tsv'].read_text().splitlines()) > 1, 'Quantity objective did not execute'
            artifacts.append(digests)
            stage.update(matching_calls=len(matching), artifact_sha256=digests)
            write(status, state)
        differences = {k: [configs[0].get(k), configs[1].get(k)]
                       for k in configs[0].keys() | configs[1].keys()
                       if configs[0].get(k) != configs[1].get(k)}
        assert set(differences) == {'jobs', 'SllReportDirectory'}
        comparison = dict(configuration_differences=differences,
                          matching_call_counts=[len(v) for v in matches],
                          matching_trajectory_identical=matches[0] == matches[1],
                          artifact_identical={k: artifacts[0][k] == artifacts[1][k] for k in artifacts[0]},
                          input_hashes_identical=state['stages'][0]['input_hashes'] == state['stages'][1]['input_hashes'],
                          scope='One sequential same-host pair. Search counters are not full visitation traces. No Vivado backend.')
        state['comparison'] = comparison
        write(output / 'comparison.json', comparison)
        assert comparison['input_hashes_identical']
        assert comparison['matching_trajectory_identical'], 'Matching results differ'
        assert all(comparison['artifact_identical'].values()), 'Placement or SLL trajectory differs'
        state['state'] = 'passed'
    except Exception:
        state.update(state='failed', error=traceback.format_exc())
        raise
    finally:
        state['finished'] = datetime.datetime.now().astimezone().isoformat()
        write(status, state)


if __name__ == '__main__':
    main()
