"""Run a frozen-routing period sensitivity check; never write a checkpoint."""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path('/Projects/jinyang/workspace/AMFplacer3.0')
SOURCE = ROOT / 'experiments/runs/clk-fpga08-u250-vivado-10ns-20260929-194539-494632/reports/vivado_routed.dcp'
EXPECTED_SHA = '7dc4819d18418c6475e88cef0ddbceca920b2293c3c223e2e766a0eeb01ab6e4'
VIVADO = '/Projects/Xilinx/Vivado/2024.2/bin/vivado'

def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()

def main():
    out = Path(sys.argv[1]).resolve()
    if not out.is_relative_to(ROOT / 'experiments/evidence'):
        raise RuntimeError('Output must be in project experiments/evidence')
    out.mkdir(parents=True, exist_ok=True)
    manifest_path = out / 'manifest.json'
    if manifest_path.exists():
        raise RuntimeError('Refusing to overwrite an existing diagnostic')
    script = Path(__file__).with_suffix('.tcl')
    source_sha = sha256(SOURCE)
    if source_sha != EXPECTED_SHA:
        raise RuntimeError('Source routed DCP hash differs from recorded baseline')
    command = [VIVADO, '-mode', 'batch', '-source', str(script), '-log', str(out / 'vivado.log'), '-journal', str(out / 'vivado.jou'), '-tclargs', str(SOURCE), str(out)]
    manifest = {
        'purpose': 'STA only on frozen routing: all 30 clocks 10 ns versus 20 ns, rising edges phase zero',
        'source_dcp': str(SOURCE), 'input_sha256': source_sha,
        'git_commit': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'git_status': subprocess.check_output(['git', '-C', str(ROOT), 'status', '--short'], text=True),
        'script_sha256': sha256(script), 'runner_sha256': sha256(Path(__file__)),
        'tool_version': subprocess.check_output([VIVADO, '-version'], text=True),
        'command': command, 'periods_ns': [10, 20], 'threads': 4,
        'checkpoint_written': False, 'state': 'running', 'started_at': now(),
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    started = time.monotonic()
    with (out / 'console.log').open('w') as log:
        result = subprocess.run(command, cwd=out, stdout=log, stderr=subprocess.STDOUT)
    manifest.update(finished_at=now(), elapsed_seconds=time.monotonic() - started, exit_code=result.returncode, source_sha256_after=sha256(SOURCE))
    complete = 'PERIOD_SENSITIVITY_COMPLETE' in (out / 'console.log').read_text()
    manifest['state'] = 'completed' if result.returncode == 0 and complete and manifest['source_sha256_after'] == source_sha else 'failed'
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: manifest[k] for k in ['state', 'exit_code', 'elapsed_seconds', 'source_sha256_after']}, indent=2), flush=True)
    if (out / 'metrics.tsv').exists():
        print((out / 'metrics.tsv').read_text(), flush=True)
    return 0 if manifest['state'] == 'completed' else 1

if __name__ == '__main__':
    sys.exit(main())
