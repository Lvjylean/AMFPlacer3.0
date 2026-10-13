#!/usr/bin/env python3
"""Build and exercise independent SLR/HPIO budgets; never launches a full design."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import time


def run(root, build, output, test_binary=None, checks=None):
    checks = checks or ["budget", "geometry", "entry-compatibility"]
    output.mkdir(parents=True, exist_ok=False)
    record = dict(root=str(root), build=str(build), state="running", stages=[],
                  created=datetime.datetime.now().astimezone().isoformat())
    def save():
        (output/"manifest.json").write_text(json.dumps(record, indent=2)+"\n")
    def command(name, argv, timeout):
        started = time.monotonic()
        with (output/(name+".log")).open("w") as log:
            try:
                result = subprocess.run(argv, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
                code = result.returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        record["stages"].append(dict(name=name, command=argv, exit_code=code,
                                     elapsed_seconds=time.monotonic()-started))
        save()
        if code != 0:
            raise RuntimeError(name+" failed; see "+str(output/(name+".log")))
    save()
    try:
        manifest=json.loads((build/"manifest.json").read_text())
        assert manifest["state"] == "completed", "The frozen full build is incomplete"
        if test_binary is None:
            command("compile-checks", ["cmake","--build",str(build/"build"),"--parallel","2","--target",
                                      "checkPaperBoundaryClustering","checkHierarchicalBoundaryPolicy"], 600)
        binary=test_binary or build/"build/checkPaperBoundaryClustering"
        record["test_binary"]=str(binary)
        record["driver_sha256"]=hashlib.sha256((root/"tests/check_paper_boundary_clustering.py").read_bytes()).hexdigest()
        record["binary_sha256"]=hashlib.sha256(binary.read_bytes()).hexdigest()
        command("policy", [str(build/"build/checkHierarchicalBoundaryPolicy")], 60)
        driver=["python3",str(root/"tests/check_paper_boundary_clustering.py"),"--root",str(root),"--binary",str(binary)]
        if "budget" in checks:
            command("budget",driver+["--output",str(output/"budget"),"--budget-only"],1200)
        if "geometry" in checks:
            command("geometry",driver+["--output",str(output/"geometry")],1200)
        if "entry-compatibility" in checks:
            command("entry-compatibility",driver+["--output",str(output/"entry-compatibility"),"--stop-on-region-entry"],1200)
        record["state"]="completed"
    except Exception as error:
        record["state"]="failed";record["error"]=str(error);save();raise
    save()
    print(json.dumps(record))

if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    for key in ("root","build","output"):parser.add_argument("--"+key,type=Path,required=True)
    parser.add_argument("--test-binary",type=Path)
    parser.add_argument("--checks",nargs="+",choices=["budget","geometry","entry-compatibility"])
    args=parser.parse_args();run(args.root.resolve(),args.build.resolve(),args.output.resolve(),
                                args.test_binary.resolve() if args.test_binary else None,args.checks)
