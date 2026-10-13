#!/usr/bin/env python3
"""Freeze and run read-only full primitive rewrite checks on existing exports."""
from pathlib import Path
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import time


def main():
    source, protected, aliases, output = [Path(p).resolve() for p in sys.argv[1:5]]
    output.mkdir(parents=True, exist_ok=False)
    (output / 'checker').mkdir()
    (output / 'logs').mkdir()
    names = ['verify_minimap2_full_logic.py', 'verify_minimap2_logic_rewrites.py',
             'compare_minimap2_functional_netlists.py', Path(__file__).name]
    for name in names:
        shutil.copy2(Path(__file__).with_name(name), output / 'checker' / name)
    root = Path(__file__).resolve().parents[2]
    audit = json.loads((source / 'manifest.json').read_text())
    manifest = dict(started=datetime.datetime.now().astimezone().isoformat(),
                    source=str(source), protected=str(protected), aliases=str(aliases),
                    checkpoints=audit['checkpoints'],
                    scripts={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in (output / 'checker').glob('*.py')},
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                    build_source='Installed Vivado 2024.2; no AMF build or placement/routing rerun',
                    python_version=sys.version, dcp_storage='server-only')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    start = time.monotonic()

    def status(state, **kwargs):
        (output / 'status.json').write_text(json.dumps(dict(
            state=state, at=datetime.datetime.now().astimezone().isoformat(),
            elapsed_seconds=time.monotonic()-start, **kwargs), indent=2))

    try:
        status('waiting_for_protected_exports')
        while True:
            files = [p / 'status.json' for p in (protected, aliases)]
            if all(f.exists() for f in files):
                jobs = {str(i)+k: v for i,f in enumerate(files) for k,v in json.loads(f.read_text()).items()}
                if any(j.get('exit_code', 0) != 0 for j in jobs.values()):
                    raise ValueError('A protected primitive export failed')
                if all(j.get('state') == 'completed' for j in jobs.values()):
                    break
            time.sleep(15)
        for label, cp in manifest['checkpoints'].items():
            meta = dict(line.split('\t', 1) for line in
                        (protected / label / 'completion.tsv').read_text().splitlines())
            if meta['input_sha256'] != cp['sha256'] or meta['vivado'] != '2024.2':
                raise ValueError('Checkpoint or tool mismatch: ' + label)
        status('checking')
        command = [sys.executable, '-B', str(output / 'checker/verify_minimap2_full_logic.py'),
                   str(source), str(protected), str(aliases), str(output / 'comparison')]
        with (output / 'logs/comparison.log').open('w') as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        status('completed' if result.returncode == 0 else 'failed', exit_code=result.returncode)
    except Exception as error:
        status('failed', error=str(error))
        raise


if __name__ == '__main__':
    main()
