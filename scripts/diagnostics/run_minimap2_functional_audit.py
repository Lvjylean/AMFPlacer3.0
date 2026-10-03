#!/usr/bin/env python3
"""Export existing MiniMap2 checkpoints; do not rerun placement or routing."""
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PREP = ROOT / 'experiments/preflight/20260930-minimap2-u250-full-02'
AMF = ROOT / 'experiments/runs/amf3-minimap2-u250-full-r10-import-repair-full-20260930-211203-742548'
NATIVE = ROOT / 'experiments/runs/vivado-minimap2-u250-route-recovery-8ns-20261001-110835-819552'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).with_name('export_minimap2_functional_netlist.tcl')
    shutil.copy2(script, output / script.name)
    shutil.copy2(__file__, output / Path(__file__).name)
    manifest = dict(started=datetime.datetime.now().astimezone().isoformat(),
                    scope='read-only source encryption scan and functional netlist export',
                    source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    tcl_sha256=sha(script), runner_sha256=sha(Path(__file__)),
                    build_source='installed Vivado 2024.2; no new AMF build',
                    dcp_storage='server-only', checkpoints={})
    (output / 'git_status.txt').write_text(subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True))
    pattern = re.compile(rb'(?:`pragma\s+protect|pragma\s+protect|`protect|begin_protected)', re.I)
    scan = {}
    for label, root in [('active_rtl', PREP / 'full-inputs/rtl'),
                        ('generated_ip', PREP / 'vivado_full'),
                        ('original', ROOT / 'data/reference/amf2-cases-20260930/projects/minimap2')]:
        files = []
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.suffix.lower() not in ('.v', '.sv', '.vhd', '.vhdl', '.vp'):
                continue
            data = path.read_bytes()
            match = pattern.search(data)
            files.append(dict(path=str(path.relative_to(root)), sha256=hashlib.sha256(data).hexdigest(),
                              encrypted_marker=bool(match),
                              marker_line=data[:match.start()].count(b'\n') + 1 if match else None))
        scan[label] = dict(root=str(root), scanned=len(files), encrypted_files=sum(f['encrypted_marker'] for f in files), files=files)
    (output / 'encryption_scan.json').write_text(json.dumps(scan, indent=2) + '\n')
    inputs = {'input': PREP / 'board-preparation-retry4/board_prepared.dcp',
              'amf3': AMF / 'reports/getrf_routed.dcp',
              'native': NATIVE / 'reports/vivado_routed.dcp'}
    states = {}
    for label, path in inputs.items():
        manifest['checkpoints'][label] = dict(path=str(path), sha256=sha(path))
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for label, path in inputs.items():
        cmd = ['/Projects/Xilinx/Vivado/2024.2/bin/vivado', '-mode', 'batch', '-notrace', '-nojournal',
               '-log', str(output / (label + '_vivado.log')), '-source', str(output / script.name),
               '-tclargs', str(path), str(output / label)]
        states[label] = dict(state='running', command=cmd)
        (output / 'status.json').write_text(json.dumps(states, indent=2) + '\n')
        start = time.monotonic()
        with (output / (label + '.log')).open('w') as log:
            code = subprocess.call(cmd, cwd=output, stdout=log, stderr=subprocess.STDOUT)
        states[label].update(state='completed' if code == 0 else 'failed', exit_code=code,
                             elapsed_seconds=time.monotonic() - start)
        (output / 'status.json').write_text(json.dumps(states, indent=2) + '\n')
        print(label, states[label]['state'], flush=True)
    manifest['finished'] = datetime.datetime.now().astimezone().isoformat()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
