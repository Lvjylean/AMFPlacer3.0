#!/usr/bin/env python3
"""Run existing source regressions without opening any experimental DCP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    results = []
    env = dict(os.environ, PYTHONPATH=str(ROOT/'scripts'), PYTHONDONTWRITEBYTECODE='1')
    def run(name, command, stdin=None):
        start = time.monotonic()
        p = subprocess.run(command, cwd=ROOT, env=env, input=stdin, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        results.append(dict(name=name, command=[str(x) for x in command], exit_code=p.returncode,
                            seconds=time.monotonic()-start, output=p.stdout))
        print(name, 'PASS' if p.returncode == 0 else 'FAIL', flush=True)
        if p.returncode:
            print(p.stdout)
            raise RuntimeError(name)
    report = dict(kind='source-consolidation-regressions', fpga_experiment_launched=False,
                  results=results, state='running')
    try:
        origins = json.loads((ROOT/'docs/provenance/incremental-compilation-20261017/source-origins.json').read_text())
        for name, entry in origins['files'].items():
            assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest() == entry['sha256'], name
        report['frozen_files_verified'] = len(origins['files'])
        py = list((ROOT/'scripts').rglob('*.py')) + list((ROOT/'tests').rglob('*.py'))
        for p in py:
            compile(p.read_bytes(), str(p), 'exec')
        report['python_syntax_files'] = len(py)
        for name in ['test_parallel_safety.py', 'test_optimized_rm_contract.py',
                     'test_background_reports.py', 'test_dfx_no_static_containment.py']:
            run(name, [sys.executable, str(ROOT/'scripts'/name)])
        run('test_native_cache_contract', [sys.executable, str(ROOT/'tests/test_native_cache_contract.py')])
        with tempfile.TemporaryDirectory(prefix='amf-source-regression-') as tmp:
            tmp = Path(tmp)
            run('ooc_clock_handoff', ['tclsh', str(ROOT/'tests/incremental/test_ooc_handoff.tcl'),
                str(ROOT/'scripts/dfx_ooc_clock_handoff.tcl'), str(tmp)])
            check = tmp/'check_tcl.tcl'
            check.write_text('foreach p $argv {set f [open $p r];set s [read $f];close $f;if {![info complete $s]} {error "Incomplete Tcl: $p"}}\n')
            run('tcl_complete', ['tclsh', str(check)] + [str(x) for x in (ROOT/'scripts').rglob('*.tcl')])
            include = ROOT/'src/lib/HiFPlacer/placement/legalization'
            for name in ['check_region_column_dp', 'replay_failed_column']:
                run('compile_'+name, ['c++','-std=c++17','-O2','-I'+str(include),
                    str(ROOT/'tests/incremental/carry'/(name+'.cc')),'-o',str(tmp/name)])
                data = (ROOT/'tests/incremental/carry/first_failed_column.txt').read_text() if name.startswith('replay') else None
                run(name, [str(tmp/name)], data)
        targets = ['checkNativeStaticCache','checkNativePlacementRepair','checkBoundarySitePolicy','checkSRLSharingInput']
        run('build_native_regressions', ['cmake','--build',str(build),'--parallel','4','--target']+targets)
        for name in targets[:3]:
            run(name, [str(build/name)])
        report['input_dependent_checks'] = {
            'checkSRLSharingInput': 'Compiled only; execution requires a design config and packing table. No FPGA experiment inputs are consumed by this source check.'}
        run('capabilities', [str(build/'AMFPlacer'),'--capabilities'])
        report['state'] = 'passed'
    except Exception as error:
        report['state'] = 'failed'
        report['error'] = str(error)
        raise
    finally:
        output = args.output or build.parent/'source-validation.json'
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        print('Validation evidence:', output, flush=True)

if __name__ == '__main__':
    main()
