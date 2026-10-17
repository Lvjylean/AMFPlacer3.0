"""Create the authorized SLL experiment beside, never inside, its source baseline."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess

ROOT = Path('/Orion/jinyang/workspace/AMFplacer3')
SOURCE = ROOT / 'experiments/preflight/20261011-srl-share4/repo'
STUDY = ROOT / 'experiments/preflight/20261013-sll-quantity-01'

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def main():
    assert subprocess.check_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD'], text=True).strip() == 'e7f6ac93b76c0c798d126ee1d66eb326929f1447'
    assert not subprocess.check_output(['git', '-C', str(SOURCE), 'status', '--porcelain', '--', 'src', 'configs', 'scripts'], text=True).strip()
    STUDY.mkdir(exist_ok=False)
    repo = STUDY / 'repo'
    source_hashes = {str(p.relative_to(SOURCE)): sha(p) for folder in ('src', 'configs', 'scripts') for p in sorted((SOURCE / folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
    subprocess.run(['git', 'clone', '--no-hardlinks', str(SOURCE), str(repo)], check=True)
    subprocess.run(['git', '-C', str(repo), 'switch', '-c', 'codex/sll-quantity-qp'], check=True)
    machine = repo / 'configs/machines/eda072.json'
    cfg = json.loads(machine.read_text())
    cfg['project_root'] = str(repo)
    cfg['build_jobs'] = 8
    machine.write_text(json.dumps(cfg, indent=2) + '\n')
    config = json.loads((SOURCE / 'configs/experiments/srl-share4-weak-true-10ns.json').read_text())
    assert config['ProPrimitiveTiming'] == 'false' and config['GlobalPlacementIteration'] == '30'
    assert config['BoundaryWeakAttraction'] == 'true' and config['SRLMaxPerSLICEM'] == '4'
    config_path = repo / 'configs/experiments/sll-quantity-baseline.json'
    config_path.write_text(json.dumps(config, indent=2) + '\n')
    previous = json.loads((SOURCE.parent / 'launch.json').read_text())
    binary = Path(previous['preflight']['binary'])
    assert sha(binary) == previous['preflight']['binary_sha256']
    for row in previous['preflight']['input_hashes'].values():
        assert sha(row['path']) == row['sha256'], row['path']
    env = json.loads((SOURCE.parent / 'environment.json').read_text())
    manifest = dict(schema='sll-quantity-study-v1', created=datetime.datetime.now().astimezone().isoformat(),
                    source=str(SOURCE), source_commit=previous['preflight']['source_commit'],
                    repo=str(repo), baseline_config=str(config_path), baseline_binary=str(binary),
                    baseline_binary_sha256=sha(binary), config_sha256=sha(config_path),
                    inputs=previous['preflight']['input_hashes'], deps=env['deps'], vivado=env['vivado'],
                    host='eda099', stage_policy='independent SLR max2 + unchanged HPIO budgets',
                    ProPrimitiveTiming=False, target_clock_ns=10, amf_threads=8, seed_policy='unchanged baseline',
                    scope='steps 1-5: quantity, gradient, QP, movement diagnostics; no spatial SLL optimization')
    (STUDY / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (STUDY / 'source-protection.json').write_text(json.dumps(source_hashes, indent=2) + '\n')
    (STUDY / 'cases').mkdir()
    (STUDY / 'validation').mkdir()
    print(json.dumps({k: manifest[k] for k in ('repo', 'source_commit', 'baseline_binary', 'stage_policy')}))

if __name__ == '__main__':
    main()
