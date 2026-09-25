#!/usr/bin/env python3
"""Audit completed AMF requests against the final server-side checkpoint."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from run_face_detect_flow import digest, now, save_json
from summarize_face_detect_flow import summarize


def request_lists(text):
    blocks = re.findall(r'set result \[catch \{place_cell \{(.*?)\}\}\]', text, re.S)
    if not blocks:
        raise ValueError('No AMF placement request blocks found')
    return '\n'.join(blocks)


NORMALIZE_TCL = r'''
set f [open [lindex $argv 0] r]
set raw [read $f]
close $f
if {[llength $raw] % 2} {error "Odd placement list length"}
set targets [dict create]
set duplicates 0
foreach {name target} $raw {
    if {[dict exists $targets $name]} {
        if {[dict get $targets $name] ne $target} {error "Conflicting targets: $name"}
        incr duplicates
    }
    dict set targets $name $target
}
set f [open [lindex $argv 1] w]
dict for {name target} $targets {puts $f "$name\t$target"}
close $f
puts "[dict size $targets] $duplicates"
'''


def verify(root):
    status = json.loads((root / 'status.json').read_text())
    if status['state'] != 'completed':
        raise RuntimeError('Flow must complete before final verification')
    manifest = json.loads((root / 'manifest.json').read_text())
    audit_dir = root / 'work/audit'
    audit_dir.mkdir(exist_ok=False)
    requests = root / 'inputs/amf_placement_requests.tsv'
    raw = root / 'inputs/amf_placement_requests.list'
    raw.write_text(request_lists((root / 'placement/DumpCLBPacking-first-0.tcl').read_text()))
    normalizer = root / 'scripts/normalize_requests.tcl'
    normalizer.write_text(NORMALIZE_TCL)
    counts = subprocess.check_output(['tclsh', str(normalizer), str(raw), str(requests)], text=True).split()
    save_json(root / 'reports/placement_request_summary.json', {
        'requested_cells': int(counts[0]), 'duplicate_identical_requests': int(counts[1]),
        'conflicting_requests': 0, 'requests_sha256': digest(requests)})
    source = Path(__file__).with_name('audit_face_detect_placement.tcl')
    audit = root / 'scripts/audit_placement.tcl'
    shutil.copy2(source, audit)
    shutil.copy2(__file__, root / 'scripts/verify_flow.py')
    command = [manifest['vivado'], '-mode', 'batch', '-notrace', '-source', str(audit),
               '-tclargs', str(root / 'reports/faceDetect_amf_routed.dcp'), str(requests), str(root / 'reports')]
    if manifest['input_mode'] == 'preplacement':
        fixed = root / 'inputs/expected_fixed_cells.tsv'
        rows = (root / 'inputs/exported/faceDetect_fixedUnits').read_text().splitlines()[1:]
        fixed.write_text(''.join(f'{r[1]}\t{r[3]}/{r[5].split(".")[-1]}\n'
                                 for r in map(str.split, rows) if r))
        command.append(str(fixed))
    started = time.monotonic()
    record = {'started': now(), 'command': command, 'audit_script_sha256': digest(audit)}
    with (root / 'logs/04_placement_audit.log').open('w') as output:
        result = subprocess.run(command, cwd=audit_dir, stdout=output, stderr=subprocess.STDOUT, timeout=1800)
    record.update({'finished': now(), 'elapsed_seconds': round(time.monotonic() - started, 3),
                   'exit_code': result.returncode})
    save_json(root / 'reports/placement_audit_status.json', record)
    if result.returncode:
        raise RuntimeError('Placement audit failed; see logs/04_placement_audit.log')
    summary = summarize(root)
    save_json(root / 'reports/verification_summary.json', summary)
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_directory', type=Path)
    verify(parser.parse_args().run_directory.resolve())
