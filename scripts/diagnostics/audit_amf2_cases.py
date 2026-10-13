#!/usr/bin/env python3
"""Read-only Vivado audits of public AMF2 project checkpoints as downloads finish."""
import argparse
import collections
import json
from pathlib import Path
import subprocess
import time

from prepare_amf2_cases import SOURCES, now, save, sha

def cells(path):
    with path.open() as f:
        next(f)
        return dict(line.rstrip('\n').split('\t', 1) for line in f)

def run_one(root, case, bundle, out):
    dcp = Path(case['selected_dcp']['path'])
    destination = out / case['name']
    destination.mkdir(parents=True, exist_ok=False)
    script = root / 'scripts/diagnostics/probe_amf2_case.tcl'
    command = ['/Projects/Xilinx/Vivado/2024.2/bin/vivado', '-mode', 'batch', '-source', str(script),
               '-log', str(destination/'vivado.log'), '-journal', str(destination/'vivado.jou'),
               '-tclargs', str(dcp), str(destination/'reports')]
    result = {'case':case['name'], 'started_at':now(), 'source_dcp':str(dcp), 'input_sha256':sha(dcp),
              'command':command, 'script_sha256':sha(script), 'placement_executed':False,
              'original_amf_clock_period_ns':case['amf_clock_period_ns'], 'state':'running'}
    save(destination/'result.json',result)
    started = time.monotonic()
    with (destination/'console.log').open('w') as f:
        completed = subprocess.run(command, cwd=destination, stdout=f, stderr=subprocess.STDOUT)
    result.update(exit_code=completed.returncode, elapsed_seconds=time.monotonic()-started,
                  finished_at=now(), source_sha256_after=sha(dcp))
    result['state'] = 'completed' if completed.returncode == 0 and 'AMF2_CASE_PROBE_COMPLETE' in (destination/'console.log').read_text() else 'failed'
    if result['state'] == 'completed':
        report = destination/'reports'
        result['design'] = cells(report/'summary.tsv')
        benchmark = cells(bundle/'cases'/case['name']/'benchmark_cells.tsv')
        actual = cells(report/'leaf_cells.tsv')
        missing = sorted(benchmark.keys()-actual.keys())
        extra = sorted(actual.keys()-benchmark.keys())
        mismatch = sorted(n for n in benchmark.keys() & actual.keys() if benchmark[n] != actual[n])
        save(destination/'identity_differences.json', {'missing_in_dcp':missing,'extra_in_dcp':extra,
             'type_mismatch':[{'cell':n,'benchmark':benchmark[n],'dcp':actual[n]} for n in mismatch]})
        result['identity'] = {'benchmark_cells':len(benchmark), 'dcp_leaf_cells':len(actual),
                              'missing_in_dcp':len(missing), 'extra_in_dcp':len(extra),
                              'type_mismatch':len(mismatch), 'exact_match':not(missing or extra or mismatch)}
        result['dcp_primitive_counts'] = dict(collections.Counter(actual.values()))
        result['constraints_exported'] = str(report/'original_timing.xdc')
        result['report_warnings'] = (report/'report_warnings.txt').read_text()
        result['pair_status'] = 'identity_matched_constraints_exported' if result['identity']['exact_match'] else 'identity_mismatch_requires_reexport_or_review'
    save(destination/'result.json',result)
    print(json.dumps({k:result.get(k) for k in ['case','state','elapsed_seconds','design','identity','pair_status']},ensure_ascii=False),flush=True)
    return result

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root,bundle,out=args.root.resolve(),args.bundle.resolve(),args.output.resolve()
    if not out.is_relative_to(root/'experiments/preflight'):
        raise RuntimeError('Output must be under project preflight directory')
    out.mkdir(parents=True,exist_ok=False)
    manifest={'started_at':now(),'state':'running','bundle':str(bundle), 'script_sha256':sha(Path(__file__)),
              'git_commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
              'tool_version':subprocess.check_output(['/Projects/Xilinx/Vivado/2024.2/bin/vivado','-version'],text=True),
              'placement_executed':False,'source_dcp_modified':False}
    save(out/'manifest.json',manifest)
    pending=[s[0] for s in SOURCES if s[3]]
    results=[]
    deadline=time.monotonic()+7200
    while pending:
        progress=False
        for name in list(pending):
            path=bundle/'cases'/name/'case.json'
            if not path.exists(): continue
            case=json.loads(path.read_text())
            if case.get('selected_dcp'):
                results.append(run_one(root,case,bundle,out))
                pending.remove(name);progress=True
                save(out/'summary.json',results)
            elif case.get('project_status') in ('failed','downloaded_no_top_implementation_dcp_found'):
                results.append({'case':name,'state':'no_dcp','reason':case.get('error',case['project_status'])})
                pending.remove(name);progress=True
                save(out/'summary.json',results)
        if not progress:
            if time.monotonic()>deadline: raise RuntimeError('Download wait limit reached; audit artifacts preserved')
            time.sleep(5)
    manifest.update(finished_at=now(),state='completed',case_count=len(results), successful_probes=sum(r['state']=='completed' for r in results))
    save(out/'manifest.json',manifest)
    print(json.dumps(manifest,ensure_ascii=False,indent=2),flush=True)

if __name__=='__main__':
    main()
