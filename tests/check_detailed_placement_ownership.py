#!/usr/bin/env python3
"""Compile the actual shortest-path method with deterministic tiny collaborators.

Old/new decisions must match. ASan/LSan and object counts must find the old
ownership failures and no leaks in the fixed method. This does not replace the
full native build or validate FPGA legality using the mock collaborators.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess

def extract(path):
    source=path.read_text()
    begin=source.index("inline float getAngle(")
    end=source.index("int ParallelCLBPacker::timingDrivenDetailedPlacement_LUTFFPairReloacationAfterSlotMapping()", begin)
    return source[begin:end]

def main():
    parser=argparse.ArgumentParser()
    for key in ("old-source","new-source","output"):
        parser.add_argument("--"+key, type=Path, required=True)
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    template=Path(__file__).with_suffix(".cc.in").read_text()
    records={}
    for label,path in (("old",args.old_source),("new",args.new_source)):
        source=out/(label+".cc")
        source.write_text(template.replace("// PRODUCTION_BODY",extract(path)))
        binary=out/label
        command=["g++","-std=c++17","-g","-O1","-fno-pie","-no-pie","-fsanitize=address,undefined",
                 "-fno-omit-frame-pointer",str(source),"-o",str(binary)]
        compile_run=subprocess.run(command,capture_output=True,text=True)
        (out/(label+"-compile.log")).write_text(compile_run.stdout+compile_run.stderr)
        if compile_run.returncode: raise RuntimeError(compile_run.stderr)
        def limit_test_output():
            resource.setrlimit(resource.RLIMIT_FSIZE,(16*1024*1024,16*1024*1024))
        with (out/(label+"-stdout.log")).open("w") as stdout, (out/(label+"-stderr.log")).open("w") as stderr:
            result=subprocess.run([str(binary)],cwd=out,stdout=stdout,stderr=stderr,
                env=dict(os.environ,ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:handle_segv=0",
                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"),timeout=30,
                preexec_fn=limit_test_output)
        stdout=(out/(label+"-stdout.log")).read_text()
        stderr=(out/(label+"-stderr.log")).read_text()
        decisions=[s for s in stdout.splitlines() if s.startswith("RESULT ")]
        leaks=[s for s in stdout.splitlines() if s.startswith("LEAK ")]
        records[label]=dict(source=str(path),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            compile_command=command,exit_code=result.returncode,decision_count=len(decisions),
            decisions=decisions,leaks=leaks,
            lsan_found_leak="LeakSanitizer: detected memory leaks" in stderr)
    assert len(records["old"]["decisions"])==len(records["new"]["decisions"])==80
    assert records["old"]["decisions"]==records["new"]["decisions"],"candidate/move decisions changed"
    assert records["old"]["exit_code"]!=0 and records["old"]["lsan_found_leak"]
    assert any(int(s.rsplit(" ",1)[1])>0 for s in records["old"]["leaks"])
    assert records["new"]["exit_code"]==0
    assert all(int(s.rsplit(" ",1)[1])==0 for s in records["new"]["leaks"])
    records["state"]="passed"
    records["limitations"]="Actual production function body, mocked collaborators; full native build and GETRF rerun required."
    (out/"result.json").write_text(json.dumps(records,indent=2)+"\n")
    print(json.dumps({"state":"passed","cases":80,"same_decisions":True,"new_exit":records["new"]["exit_code"],"output":str(out)}))

if __name__=="__main__":main()
