"""Finite experiment worker; records exit status and never changes source inputs."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time

def main():
    study = Path(__file__).resolve().parent
    m = json.loads((study / 'manifest.json').read_text())
    name, binary, config = sys.argv[1:4]
    mode = sys.argv[4] if len(sys.argv) > 4 else '--global-only'
    assert mode in ('--global-only', '--amf-only', '--full')
    out = study / 'cases' / name
    out.mkdir(exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith('AMF_')}
    env['PATH'] = m['deps'] + '/bin:' + env['PATH']
    env['LD_LIBRARY_PATH'] = m['deps'] + '/lib:' + env.get('LD_LIBRARY_PATH', '')
    env.update(AMF_MACRO_STUDY_MODE='control', AMF_MACRO_STUDY_START='1',
               AMF_MACRO_STUDY_OUTPUT=str(out / 'diagnostics'), AMF_LEGALIZATION_TRACE='1',
               AMF_MATCHER_WORKER_TRACE='1')
    (out / 'diagnostics').mkdir()
    command = [sys.executable, 'scripts/amf3.py', 'full-run', '--profile', '--allow-import-repair',
               '--config', config, '--binary', binary, '--dcp', m['inputs']['post_opt.dcp']['path'],
               '--vivado', m['vivado'], '--run-prefix', 'getrf-sll-' + name]
    if mode != '--full':
        command.append(mode)
    state = dict(name=name, state='running', command=command, host=m['host'],
                 started=datetime.datetime.now().astimezone().isoformat(),
                 binary_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                 config_sha256=hashlib.sha256(Path(config).read_bytes()).hexdigest(),
                 environment={k: v for k, v in env.items() if k.startswith('AMF_')})
    status = out / 'worker.json'
    status.write_text(json.dumps(state, indent=2) + '\n')
    started = time.monotonic()
    with (out / 'launcher.log').open('w') as log:
        proc = subprocess.Popen(command, cwd=m['repo'], env=env, stdout=log, stderr=subprocess.STDOUT)
        state['pid'] = proc.pid
        status.write_text(json.dumps(state, indent=2) + '\n')
        code = proc.wait()
    state.update(state='completed' if code == 0 else 'failed', exit_code=code,
                 elapsed_seconds=time.monotonic() - started, finished=datetime.datetime.now().astimezone().isoformat())
    # A pilot named "disabled" must not capture "disabled-final" runs.
    prefix = 'getrf-sll-' + name + ('-global-' if mode == '--global-only' else '-')
    runs = sorted((Path(m['repo']) / 'experiments/runs').glob(prefix + '*'))
    state['runs'] = [str(p) for p in runs]
    status.write_text(json.dumps(state, indent=2) + '\n')
    return code

if __name__ == '__main__':
    sys.exit(main())
