#!/usr/bin/env python3
"""Run independent prepared synthesis jobs, recording exit status and hashes."""
import concurrent.futures
import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(4*1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def save(p, obj):
    temp = p.with_suffix(p.suffix+'.tmp')
    temp.write_text(json.dumps(obj, indent=2)+'\n')
    temp.replace(p)


def run(item):
    directory = Path(item['directory'])
    status = directory/'synthesis_status.json'
    if status.exists():
        raise RuntimeError(f'Refusing to rerun {directory}')
    command = ['/Projects/Xilinx/Vivado/2024.2/bin/vivado', '-mode', 'batch', '-notrace', '-nojournal',
               '-log', 'vivado.log', '-source', str(directory/'synthesize.tcl')]
    record = dict(case=item['case'], state='running', command=command,
                  started=dt.datetime.now().astimezone().isoformat(),
                  input_manifest_sha256=digest(directory/'input_manifest.json'),
                  synthesis_script_sha256=digest(directory/'synthesize.tcl'))
    save(status, record)
    begin = time.monotonic()
    with (directory/'console.log').open('w') as log:
        p = subprocess.Popen(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
        record['pid'] = p.pid
        save(status, record)
        code = p.wait()
    record.update(state='completed' if code == 0 else 'failed', exit_code=code,
                  elapsed_seconds=time.monotonic()-begin, finished=dt.datetime.now().astimezone().isoformat())
    record['artifacts'] = {name:dict(bytes=(directory/name).stat().st_size, sha256=digest(directory/name))
                           for name in ('post_synth.dcp', 'post_opt.dcp', 'post_opt.edf') if (directory/name).exists()}
    save(status, record)
    print(json.dumps({k:v for k,v in record.items() if k!='artifacts'}), flush=True)
    return record


if __name__ == '__main__':
    root = Path(sys.argv[1]).resolve()
    items = json.loads((root/'catalog.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, items))
    good = all(r['state']=='completed' and 'post_opt.dcp' in r['artifacts'] for r in results)
    save(root/'synthesis_gate.json', dict(all_completed=good, cases=results,
                                        placement_started=False, routing_started=False))
    sys.exit(0 if good else 1)
