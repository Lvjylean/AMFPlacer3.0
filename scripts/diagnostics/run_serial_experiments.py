#!/usr/bin/env python3
"""Run an explicit experiment plan once, waiting for each process group to exit."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ACTIVE = None
STOP_SIGNAL = None


def now():
    return dt.datetime.now(dt.timezone(dt.timedelta(hours=8))).isoformat()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            h.update(block)
    return h.hexdigest()


def save(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")
    temp.replace(path)


def stop(signum, _frame):
    global STOP_SIGNAL
    STOP_SIGNAL = signum
    if ACTIVE is not None and ACTIVE.poll() is None:
        try:
            os.killpg(ACTIVE.pid, signum)
        except ProcessLookupError:
            pass


def group_members(pgid):
    rows = subprocess.check_output(["ps", "-eo", "pid=,pgid=,stat=,comm="], text=True)
    return [line.strip() for line in rows.splitlines()
            if len(line.split()) >= 3 and line.split()[1] == str(pgid)
            and not line.split()[2].startswith("Z")]


def verify(hashes):
    for path, expected in hashes.items():
        if digest(path) != expected:
            raise RuntimeError("Frozen input or runner changed: " + path)


def execute(plan_path):
    global ACTIVE
    plan_path = Path(plan_path).resolve()
    plan = json.loads(plan_path.read_text())
    root = Path(plan["root"]).resolve()
    state_path = plan_path.parent / "queue-state.json"
    state = dict(schema="serial-experiments-v1", state="starting",
                 started=now(), queue_pid=os.getpid(),
                 plan=str(plan_path), plan_sha256=digest(plan_path), jobs=[])
    # An existing state is never restarted implicitly.
    with state_path.open("x") as stream:
        json.dump(state, stream, indent=2)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        for job in plan["jobs"]:
            if STOP_SIGNAL:
                break
            verify(plan.get("common_hashes", {}))
            verify(job.get("hashes", {}))
            record = dict(id=job["id"], state="starting", command=job["command"],
                          started=now(), run_directory=None,
                          log=str(plan_path.parent / (job["id"] + ".log")))
            state["jobs"].append(record)
            state.update(state="running", active_job=job["id"])
            save(state_path, state)
            begin = time.monotonic()
            with Path(record["log"]).open("x") as output:
                ACTIVE = subprocess.Popen(job["command"], cwd=root,
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, bufsize=1,
                    start_new_session=True)
                record.update(state="running", runner_pid=ACTIVE.pid)
                save(state_path, state)
                for line in ACTIVE.stdout:
                    output.write(line)
                    output.flush()
                    candidate = line.strip()
                    if record["run_directory"] is None and candidate.startswith(str(root / "experiments/runs") + "/"):
                        path = Path(candidate)
                        if path.is_dir():
                            record["run_directory"] = str(path)
                            save(state_path, state)
                code = ACTIVE.wait()
                members = group_members(ACTIVE.pid)
                record.update(exit_code=code, elapsed_seconds=time.monotonic()-begin,
                              finished=now(), remaining_live_group_members=members)
                run = Path(record["run_directory"]) if record["run_directory"] else None
                if run and (run / "status.json").exists():
                    record["run_status"] = json.loads((run / "status.json").read_text())
                record["state"] = ("completed" if code == 0 else "failed")
                save(state_path, state)
                if members:
                    raise RuntimeError("Previous job still has live children; next job is not started")
                ACTIVE = None
                if STOP_SIGNAL:
                    break
        state["state"] = ("cancelled" if STOP_SIGNAL else
                          "completed" if all(j["state"] == "completed" for j in state["jobs"])
                          else "completed_with_failures")
        state["signal"] = STOP_SIGNAL
        state["finished"] = now()
        state.pop("active_job", None)
        save(state_path, state)
    except Exception as error:
        state.update(state="blocked", error=str(error), finished=now())
        save(state_path, state)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True)
    execute(parser.parse_args().plan)

