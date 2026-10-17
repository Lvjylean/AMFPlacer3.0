"""Routing-policy defaults, serialization, comparison identity and shell reuse."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from dfx_comparison import identity
from run_dfx_multi import validate, settings, check_shell


def main():
    configs = Path(__file__).resolve().parents[1] / "configs"
    job = validate(json.loads((configs / "getrf-v0-072-20ns.json").read_text()))
    assert job["static_routing_containment"] is False
    absent = copy.deepcopy(job)
    absent.pop("static_routing_containment")
    assert validate(absent)["static_routing_containment"] is False
    for value in ("typo", 1, None):
        try:
            validate(dict(job, static_routing_containment=value))
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid containment value accepted")
    with tempfile.TemporaryDirectory() as td:
        out = Path(td)
        (out / "inputs").mkdir()
        script = settings(job, out)
        result = subprocess.run(
            ["tclsh"], input=f"source {{{script}}}\nputs $dfx(static_routing_containment)\n",
            capture_output=True, text=True, check=True)
        assert result.stdout.strip() == "0"
        ck = out / "identity-fixture.dcp"
        ck.write_bytes(b"comparison-identity-test")
        comparison = dict(job, comparison_v1_checkpoint=str(ck),
                          comparison_v1_sha256=hashlib.sha256(ck.read_bytes()).hexdigest())
        off = identity(comparison, "same-binary", {"jobs": "12"})
        on = identity(dict(comparison, static_routing_containment=True),
                      "same-binary", {"jobs": "12"})
        assert off["placement_constraints_sha256"] != on["placement_constraints_sha256"]
        comparison["static_region_granularity"] = "clock_region"
        assert identity(comparison, "same-binary", {"jobs": "12"}) == off
        job["shell_run"] = str(out)
        old = dict(job)
        old.pop("static_routing_containment")

        def parent():
            (out / "manifest.json").write_text(json.dumps(dict(
                state="completed", schema="amf-dfx-multi-shell-v1", job=old, outputs={})))

        parent()
        try:
            check_shell(job)
        except ValueError as error:
            assert "static routing policy mismatch" in str(error)
        else:
            raise AssertionError("Historical contained shell accepted")
        old["static_routing_containment"] = False
        parent()
        check_shell(job)
    for path in configs.glob("*.json"):
        config = json.loads(path.read_text())
        if "partitions" in config:
            assert config["static_routing_containment"] is False
            assert config["static_containment_experiment"] is False
            assert config["clock_period_ns"] == 20
    print("PASS: disabled default, settings serialization, distinct identity, shell policy matching and configs")


if __name__ == "__main__":
    main()
