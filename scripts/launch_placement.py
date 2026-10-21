#!/usr/bin/env python3
"""Freeze the small placement config and dispatch one recorded experiment.

Default: read-only preflight. --start explicitly submits a single run.
visualization=true always selects 099. SSH uses existing authentication only.
"""
import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import uuid

from export_placement_config import read_json, resolve

ROOT = Path(__file__).resolve().parents[1]


def now():
    return dt.datetime.now().astimezone().isoformat()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def save(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def select_host(primary, requested, origin):
    if primary.get("visualization") == "true":
        return "099"
    return requested if requested != "auto" else (origin or "072")


def identify_host(catalog):
    hostname = socket.gethostname().split(".")[0]
    return next((key for key, target in catalog["targets"].items()
                 if hostname in target["hostnames"]), None)


def environment(target, jobs):
    env = {k: v for k, v in os.environ.items() if not k.startswith("AMF_")}
    env.update(target.get("environment", {}))
    env.update(PYTHONDONTWRITEBYTECODE="1", OMP_NUM_THREADS=jobs,
               MKL_NUM_THREADS=jobs, OPENBLAS_NUM_THREADS=jobs)
    return env


def resources(root):
    values = dict(line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
    return {"memory_available_gib": int(values["MemAvailable"].split()[0]) / 1024**2,
            "disk_free_gib": shutil.disk_usage(root).free / 1024**3}


def guard_resources(available, target):
    for resource, minimum in (("memory_available_gib", target["minimum_memory_gib"]),
                              ("disk_free_gib", target["minimum_disk_gib"])):
        if available[resource] < minimum:
            raise ValueError("%s %.2f < %.2f; no experiment started, no threshold reduced" %
                             (resource, available[resource], minimum))


def verify_file(path, expected):
    actual = digest(path)
    if actual != expected:
        raise ValueError("Frozen file hash changed: " + str(path))
    return actual


def verify_build_metadata(binary):
    path = binary.resolve().parent.parent / "manifest.json"
    record = read_json(path)
    capabilities = record.get("capabilities", {})
    if (record.get("state") != "completed"
            or capabilities.get("schema") != "amf-capabilities-v1"
            or capabilities.get("clock_resource_capacity_schema") != 1
            or record.get("binaries", {}).get("AMFPlacer") != digest(binary)):
        raise ValueError("Invalid recorded build for clock capacity: " + str(path))
    return {"path": str(path), "sha256": digest(path)}


def validate(request, catalog):
    """Read-only target preflight, including actual binary and input identities."""
    host = request["target"]
    if identify_host(catalog) != host:
        raise ValueError("Receiver is not the selected server " + host)
    primary = json.loads(request["primary_text"])
    if select_host(primary, host, host) != host:
        raise ValueError("Visualization requests must run on 099")
    if primary.get("IncrementalCompilation") != "false":
        raise ValueError("This registered case is ordinary full-run. DFX needs a matching "
                         "static-cache/module job; use export_placement_config.py with its "
                         "DFX base. Never send an incremental job through full-run.")
    target = catalog["targets"][host]
    case = catalog["cases"][request["case"]][host]
    root = Path(target["root"])
    base_path = root / case["base"]
    verify_file(base_path, case["base_sha256"])
    base = read_json(base_path)
    effective, report = resolve(primary, base, target["profile"])
    if float(primary["ClockPeriod"]) != float(case["vivado_clock_period_ns"]):
        raise ValueError("AMF period differs from this prepared DCP. Register matching "
                         "Vivado constraints/provenance before a complete experiment.")
    binary = root / target["binary"]
    helper = binary.with_name("partitionHyperGraph")
    verify_file(binary, target["binary_sha256"])
    verify_file(helper, target["helper_sha256"])
    capabilities = json.loads(subprocess.check_output(
        [str(binary), "--capabilities"], text=True,
        env=environment(target, primary["jobs"]), timeout=20))
    for key, value in target["required_capabilities"].items():
        if capabilities.get(key) != value:
            raise ValueError("Binary lacks required capability: " + key)
    build_metadata = (verify_build_metadata(binary)
                      if effective.get("clock resource capacity file") else None)
    verified = {}
    for key, record in case["inputs"].items():
        path = record["path"]
        if key != "post_opt.dcp" and base.get(key) != path:
            raise ValueError("Input registry disagrees with base: " + key)
        verified[key] = {"path": path, "sha256": verify_file(path, record["sha256"])}
    for item in case["support_files"]:
        verify_file(item["path"], item["sha256"])
    for path, sha in target["flow_files"].items():
        verify_file(root / path, sha)
    provenance = root / case["provenance"]
    verify_file(provenance, case["provenance_sha256"])
    for path in (root/"scripts/amf3.py", Path(target["vivado"])):
        if not path.is_file():
            raise ValueError("Missing executable: " + str(path))
    # Output paths are owned by the new run, never inherited from an old run.
    effective.pop("dumpDirectory", None)
    effective.pop("BoundaryReportDirectory", None)
    unmanaged = [k for k, v in effective.items()
                 if (k.startswith("Dump") and Path(v).is_absolute())
                 or k.endswith("report file") or k.startswith(("DFX ", "Native "))
                 or k in ("SllReportDirectory",)]
    if unmanaged:
        raise ValueError("Unregistered writable/context fields: " + ", ".join(unmanaged))
    report.update(target=host, binary=str(binary), binary_sha256=target["binary_sha256"],
                  capabilities=capabilities, build_metadata=build_metadata, inputs=verified,
                  resources=resources(root), experiment_started=False,
                  source_config_path=request["config_origin"],
                  source_config_sha256=hashlib.sha256(request["primary_text"].encode()).hexdigest(),
                  base_path=str(base_path), base_sha256=case["base_sha256"],
                  synthesis_executed=False, input_export_executed=False)
    return effective, report


def command_for(request, catalog, job):
    target = catalog["targets"][request["target"]]
    case = catalog["cases"][request["case"]][request["target"]]
    root = Path(target["root"])
    return ["/usr/bin/python3", str(root/"scripts/amf3.py"), "full-run",
            "--config", str(job/"amf_config.json"),
            "--binary", str(root/target["binary"]), "--dcp", case["inputs"]["post_opt.dcp"]["path"],
            "--vivado", target["vivado"], "--input-provenance", str(job/"input-provenance.json"),
            "--run-prefix", "cfg-" + request["id"], "--allow-import-repair"]


def receive(request, catalog):
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,95}", request["id"]):
        raise ValueError("Invalid request name")
    target = catalog["targets"][request["target"]]
    if identify_host(catalog) != request["target"]:
        raise ValueError("Wrong target host")
    job = Path(target["root"])/"experiments/config-dispatch"/request["id"]
    if request["action"] == "start" and job.exists():
        original = read_json(job/"request.json")
        if original != request:
            raise ValueError("Request name already belongs to another configuration")
        return {"existing_request": str(job), "status": read_json(job/"status.json"),
                "resubmitted": False}
    effective, report = validate(request, catalog)
    if request["action"] == "check":
        return report
    if request["action"] != "start":
        raise ValueError("Unknown request action")
    guard_resources(report["resources"], target)
    job.mkdir(parents=True, exist_ok=False)  # exclusive even for concurrent dispatch
    save(job/"request.json", request)
    save(job/"catalog.json", catalog)
    (job/"placer.json").write_text(request["primary_text"], encoding="utf-8")
    save(job/"amf_config.json", effective)
    save(job/"resolution.json", report)
    case = catalog["cases"][request["case"]][request["target"]]
    for source, dest in [
        (Path(target["root"])/case["base"], "base.json"),
        (Path(target["root"])/case["provenance"], "input-provenance.json"),
        (Path(__file__), "launch_placement.py"),
        (Path(__file__).with_name("export_placement_config.py"), "export_placement_config.py"),
    ]:
        shutil.copy2(source, job/dest)
    save(job/"status.json", {"state": "submitted", "submitted": now(), "experiment_started": False})
    try:
        with (job/"controller.log").open("x") as log:
            process = subprocess.Popen(
                [sys.executable, str(job/"launch_placement.py"), "--worker", str(job)],
                cwd=target["root"], env=environment(target, effective["jobs"]),
                stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True)
        save(job/"submission.json", {"pid": process.pid, "time": now()})
    except Exception as error:
        save(job/"status.json", {"state": "dispatch_failed", "error": str(error),
                                "experiment_started": False, "finished": now()})
        raise
    return {"request_directory": str(job), "controller_pid": process.pid,
            "target": request["target"], "state": "submitted"}


def worker(job):
    """Run once; failures are recorded, never retried automatically."""
    try:
        with (job/"worker-claim.json").open("x") as claim:
            json.dump({"pid": os.getpid(), "time": now()}, claim)
    except FileExistsError:
        return 1
    request = read_json(job/"request.json")
    catalog = read_json(job/"catalog.json")
    target = catalog["targets"][request["target"]]
    status = {"state": "preflight", "started": now(), "experiment_started": False}
    start = time.monotonic()
    try:
        with (job.parent/"active.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            effective, report = validate(request, catalog)
            if effective != read_json(job/"amf_config.json"):
                raise ValueError("Frozen effective configuration changed")
            guard_resources(report["resources"], target)
            command = command_for(request, catalog, job)
            save(job/"command.json", {"argv": command, "cwd": target["root"]})
            with (job/"experiment.log").open("x") as log:
                process = subprocess.Popen(command, cwd=target["root"],
                                           env=environment(target, effective["jobs"]),
                                           stdin=subprocess.DEVNULL, stdout=log,
                                           stderr=subprocess.STDOUT)
                status.update(state="running", experiment_started=True, pid=process.pid)
                save(job/"status.json", status)
                code = process.wait()
            status.update(exit_code=code,
                          state="completed_pending_review" if code == 0 else "failed")
            status["run_directories"] = [str(p) for p in
                (Path(target["root"])/"experiments/runs").glob("cfg-"+request["id"]+"-*")]
    except Exception as error:
        status.update(state="failed", error=str(error))
    status.update(finished=now(), wall_seconds=time.monotonic()-start)
    save(job/"status.json", status)
    return 0 if status.get("exit_code") == 0 else 1


def transport(target, command, payload=None):
    remote = shlex.join(command)
    result = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
                             target["ssh"], remote], input=payload, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if result.returncode:
        raise ValueError("SSH dispatch failed (no fallback to a different host): " +
                         result.stderr.strip() +
                         ". From the local project use --config-host 072 to read the "
                         "072 config and dispatch through existing local SSH access.")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", default=str(ROOT/"configs/placement/hosts.json"))
    parser.add_argument("--config", help="Small JSON file; defaults to this copy's placer.json")
    parser.add_argument("--config-host", choices=("072", "099"),
                        help="Read that server's current small config; route after reading")
    parser.add_argument("--host", choices=("auto", "072", "099"), default="auto")
    parser.add_argument("--case", default="getrf-prepared")
    parser.add_argument("--name", help="Unique request name; repeating a submitted name never restarts it")
    parser.add_argument("--start", action="store_true", help="Submit once; otherwise preflight only")
    parser.add_argument("--receive", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--worker", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        return worker(args.worker)
    catalog = read_json(args.catalog)
    if args.receive:
        result = receive(json.load(sys.stdin), catalog)
    else:
        origin = identify_host(catalog)
        if args.config_host and args.config:
            raise ValueError("Use either --config or --config-host")
        if args.config_host:
            source = catalog["targets"][args.config_host]
            path = str(Path(source["root"])/"configs/placement/placer.json")
            raw = (Path(path).read_text(encoding="utf-8") if origin == args.config_host
                   else transport(source, ["cat", path]))
            config_origin = args.config_host + ":" + path
        else:
            path = Path(args.config) if args.config else ROOT/"configs/placement/placer.json"
            raw = path.read_text(encoding="utf-8")
            config_origin = str(path.resolve())
        # Validate JSON syntax/duplicates using the generator's parser semantics.
        from export_placement_config import unique_object
        primary = json.loads(raw, object_pairs_hook=unique_object)
        host = select_host(primary, args.host, origin)
        request = {"schema": 1, "action": "start" if args.start else "check",
                   "id": args.name or "placement-" + uuid.uuid4().hex[:12],
                   "case": args.case, "target": host, "primary_text": raw,
                   "config_origin": config_origin}
        if origin == host:
            result = receive(request, catalog)
        else:
            target = catalog["targets"][host]
            result = json.loads(transport(
                target, ["python3", str(Path(target["root"])/"scripts/launch_placement.py"),
                         "--receive"], json.dumps(request)))
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        raise SystemExit("Placement dispatch error: " + str(error))
