"""Regression checks for geometry identity and immutable shell reuse."""
from pathlib import Path
import hashlib
import json
import tempfile
from dfx_comparison import identity
from run_dfx_multi import validate, check_shell

def main():
    job=dict(IncremenralCompilation=True,static_routing_containment=True,part="xcu250-figd2104-2L-e",
             clock_name="clk",clock_period_ns=10,partitions=[
                 dict(id="a",active_partition="rp_a",pblock="pblock_a",
                      pblock_ranges=["CLOCKREGION_X1Y1:CLOCKREGION_X3Y1"],expected_slr="SLR0"),
                 dict(id="b",active_partition="rp_b",pblock="pblock_b",
                      pblock_ranges=["CLOCKREGION_X1Y5:CLOCKREGION_X3Y5"],expected_slr="SLR1")])
    validate(job)
    with tempfile.TemporaryDirectory() as td:
        t=Path(td);ck=t/"dummy.dcp";ck.write_bytes(b"comparison-test-only")
        job["comparison_v1_checkpoint"]=str(ck)
        job["comparison_v1_sha256"]=hashlib.sha256(ck.read_bytes()).hexdigest()
        a=identity(job,"binary",{"jobs":"12"})
        job["static_region_granularity"]="site"
        assert identity(job,"binary",{"jobs":"12"})==a
        job["static_region_granularity"]="clock_region";validate(job)
        b=identity(job,"binary",{"jobs":"12"})
        assert a["placement_constraints_sha256"]!=b["placement_constraints_sha256"]
        old=dict(job);old.pop("static_region_granularity");job["shell_run"]=str(t)
        def parent():
            (t/"manifest.json").write_text(json.dumps(dict(state="completed",schema="amf-dfx-multi-shell-v1",job=old,outputs={})))
        parent()
        try:check_shell(job)
        except ValueError as e:assert "granularity mismatch" in str(e)
        else:raise AssertionError("accepted old shell")
        old["static_region_granularity"]="clock_region";parent();check_shell(job)
        job["static_region_granularity"]="typo"
        try:validate(job)
        except ValueError as e:assert "static_region_granularity" in str(e)
        else:raise AssertionError("invalid mode accepted")
    print("PASS: legacy identity retained; new geometry distinguished; old shell rejected; matching shell accepted; invalid mode rejected")

if __name__=="__main__":main()
