"""Canonical, path-independent identities for full/incremental DFX comparison."""
import hashlib
import json
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def identity(job, binary_sha256, amf_config):
    checkpoint = job.get("comparison_v1_checkpoint")
    if not checkpoint:
        return None  # Functional runs are valid, but cannot pass performance acceptance.
    actual = hashlib.sha256(Path(checkpoint).read_bytes()).hexdigest()
    if actual != job.get("comparison_v1_sha256"):
        raise ValueError("Comparison V1 checkpoint hash mismatch")
    constraints = {k:job[k] for k in ("part", "clock_name", "clock_period_ns")}
    constraints["partitions"] = [{k:r.get(k) for k in ("id", "active_partition", "pblock", "pblock_ranges", "expected_slr")} for r in job["partitions"]]
    enabled = job.get("static_routing_containment", False) in (True, "true")
    if not enabled:
        constraints["static_region"] = "no_static_pblock;modeled_sites_outside_rp_placement_footprints;static_routing_uncontained"
    elif job.get("static_region_granularity", "site") == "clock_region":
        constraints["static_region"] = "whole_clock_regions_excluding_actual_rp_routing_footprints;CONTAIN_ROUTING=true;IS_SOFT=false"
    else:
        constraints["static_region"] = "complement_of_actual_rp_routing_footprints;CONTAIN_ROUTING=true;IS_SOFT=false"
    if 'srl_control_metadata_semantics' in job:
        constraints['srl_control_metadata_semantics'] = job['srl_control_metadata_semantics']
    config = dict(amf_config)
    for k,v in list(config.items()):
        if k in ("vivado extracted design information file", "clock file", "dumpDirectory", "BoundaryReportDirectory"):
            del config[k]
        elif "file" in k or k == "mergedSharedCellType2sharedCellType":
            config[k] = hashlib.sha256(Path(v).read_bytes()).hexdigest()
    return dict(v1_design_input_sha256=actual, placement_constraints_sha256=digest(constraints),
                amf_binary_sha256=binary_sha256, amf_settings_sha256=digest(config))
