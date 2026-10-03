#!/usr/bin/env python3
"""Finish the current experiment's report after its four AMF jobs exit."""
import argparse
import datetime as dt
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--evidence", default="experiments/evidence/20260929-placement-ablation")
    args = parser.parse_args()
    evidence = args.root / args.evidence
    with (evidence / "collector.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        started = time.monotonic()
        while time.monotonic() - started < 8 * 3600:
            proc = subprocess.run([sys.executable, str(args.root / "scripts/diagnostics/analyze_placement_ablation.py"),
                                   "--root", str(args.root), "--evidence", args.evidence],
                                  capture_output=True, text=True)
            if proc.returncode:
                state = dict(state="analysis_failed", error=proc.stderr[-4000:])
            else:
                comparison = json.loads(proc.stdout)
                state = dict(state="completed" if comparison["all_finished"] else "waiting",
                             checked=comparison["checked"], pid=os.getpid())
            (evidence / "collector_status.json").write_text(json.dumps(state, indent=2) + "\n")
            if state["state"] != "waiting":
                print(json.dumps(state), flush=True)
                return
            time.sleep(60)
        (evidence / "collector_status.json").write_text(json.dumps(dict(
            state="timeout", checked=dt.datetime.now().astimezone().isoformat()), indent=2) + "\n")


if __name__ == "__main__":
    main()
