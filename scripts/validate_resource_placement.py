"""Record a Vivado round-trip audit of partial hard-resource placement."""
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import time

from inspect_amf_inputs import digest


def validate(root, args):
    from amf3 import git, machine, save, stamp
    root = Path(root)
    source = (root / args.resource_run).resolve()
    dcp = (root / args.dcp).resolve()
    if json.loads((source / 'status.json').read_text())['state'] != 'completed':
        raise ValueError('Resource legalization must have completed successfully')
    directory = root / 'experiments/runs' / ('getrf-u250-resources-' + stamp())
    inputs = directory / 'inputs'
    reports = directory / 'reports'
    inputs.mkdir(parents=True, exist_ok=False)
    reports.mkdir()
    for name in ['resources.tsv', 'resources.json', 'place_resources.tcl']:
        shutil.copy2(source / name, inputs / name)
    script = inputs / 'validate_resource_placement.tcl'
    shutil.copy2(root / 'scripts/validate_resource_placement.tcl', script)
    command = [machine()['vivado'], '-mode', 'batch', '-nojournal', '-log', str(directory / 'vivado.log'),
               '-source', str(script), '-tclargs', str(dcp), str(inputs), str(reports)]
    save(directory / 'manifest.json', dict(
        schema='amf-vivado-resource-run-v1', source_commit=git('rev-parse', 'HEAD'),
        git_status=git('status', '--porcelain'), resource_run=str(source),
        resource_manifest_sha256=digest(source / 'manifest.json'),
        input_dcp=str(dcp), input_dcp_sha256=digest(dcp), command=command,
        inputs_sha256={p.name: digest(p) for p in sorted(inputs.iterdir())},
        started=dt.datetime.now().astimezone().isoformat(), dcp_storage='server-only',
        full_placement_executed=False, routing_executed=False))
    save(directory / 'status.json', dict(state='running'))
    print(directory, flush=True)
    start = time.monotonic()
    with (directory / 'console.log').open('w') as log:
        result = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
    status = dict(state='completed' if result.returncode == 0 else 'failed', exit_code=result.returncode,
                  elapsed_seconds=time.monotonic() - start, finished=dt.datetime.now().astimezone().isoformat())
    if result.returncode == 0:
        validation = json.loads((reports / 'validation.json').read_text())
        expected = json.loads((inputs / 'resources.json').read_text())['assigned_cells']
        if validation['matched_cells'] != expected:
            status.update(state='failed', error='Vivado matched cell count differs from AMF assignment')
        status['output_dcp_sha256'] = digest(reports / 'getrf_hard_resources_partial.dcp')
    save(directory / 'status.json', status)
    if status['state'] != 'completed':
        raise RuntimeError('Vivado resource validation failed: ' + str(directory / 'console.log'))
    print(json.dumps(status))
