#!/usr/bin/env python3
"""Download allowlisted reports from eda072; final DCP remains server-side."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
from amf3 import validate_run_id
from report_bundle import extract_bundle

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_id', type=validate_run_id)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--ssh-hostname', help='Override HostName while retaining the configured SSH alias and identity')
    args = parser.parse_args()
    settings = json.loads((ROOT / 'configs/machines/eda072.json').read_text())
    remote_root = settings['project_root']
    remote_code = '\n'.join([
        'import sys',
        'from pathlib import Path',
        'sys.path.insert(0, ' + repr(remote_root + '/scripts') + ')',
        'from report_bundle import write_bundle',
        'write_bundle(Path(' + repr(remote_root + '/experiments/runs/' + args.run_id) + '), sys.stdout.buffer)',
    ])
    command = ['ssh']
    if args.ssh_hostname:
        command.extend(['-o', 'HostName=' + args.ssh_hostname])
    command.extend([settings['ssh_alias'], 'python3 -c ' + shlex.quote(remote_code)])
    blob = subprocess.check_output(command)
    destination = args.destination or ROOT / 'local-reports' / args.run_id
    count = extract_bundle(blob, destination)
    print(json.dumps({'files': count, 'destination': str(destination.resolve()), 'dcp_downloaded': False}, indent=2))


if __name__ == '__main__':
    main()
