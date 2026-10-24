#!/usr/bin/env python3
"""Build/run focused synthetic spreading checks; never launch an AMF flow."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path,
                        help="Existing four-LUT synthetic fixture with physical SLR metadata")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build, fixture, output = args.build_dir.resolve(), args.fixture.resolve(), args.output.resolve()
    if not fixture.is_file():
        parser.error("fixture does not exist")
    manifest = json.loads((build.parent / "manifest.json").read_text())
    if manifest["state"] != "completed":
        parser.error("a completed frozen build is required")
    output.mkdir(parents=True, exist_ok=False)
    result = {"build": str(build), "fixture": str(fixture), "commands": [],
              "scope": "synthetic helper tests only; no AMF placement experiment or Vivado",
              "started": datetime.datetime.now().astimezone().isoformat()}
    def save():
        (output / "checks.json").write_text(json.dumps(result, indent=2) + "\n")
    def run(name, command):
        start = time.monotonic()
        argv = list(map(str, command))
        with (output / (name + ".log")).open("w") as log:
            proc = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT)
        result["commands"].append({"name": name, "argv": argv, "exit_code": proc.returncode,
                                   "wall_seconds": time.monotonic() - start})
        save()
        if proc.returncode:
            raise RuntimeError(name + " failed; preserve its log")
    try:
        targets = ["checkDieFirstSpreading", "checkDieFirstSpreadingIntegration", "checkSpreadRegionLimit",
                   "checkSpreadDisplacement", "checkSllAdaptivePolicy", "checkSllAdaptiveIntegration", "checkSllQuantity"]
        run("compile-tests", ["cmake", "--build", build, "--parallel", "2", "--target", *targets])
        run("die-first-region", [build / targets[0], fixture])
        run("die-first-single-writeback", [build / targets[1], fixture, output / "single-writeback"])
        run("die-first-macro-writeback", [build / targets[1], fixture, output / "macro-writeback", "macro"])
        run("region-limit", [build / "checkSpreadRegionLimit", fixture])
        run("displacement", [build / "checkSpreadDisplacement"])
        run("sll-policy", [build / "checkSllAdaptivePolicy"])
        run("sll-quantity", [build / "checkSllQuantity"])
        run("sll-integration", [build / "checkSllAdaptiveIntegration", fixture, output / "sll-integration"])
        result["state"] = "passed"
    except Exception as error:
        result["state"], result["error"] = "failed", str(error)
        raise
    finally:
        result["finished"] = datetime.datetime.now().astimezone().isoformat()
        save()


if __name__ == "__main__":
    main()
