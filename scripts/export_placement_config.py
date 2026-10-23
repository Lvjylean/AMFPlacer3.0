#!/usr/bin/env python3
"""Apply common human-editable placement settings to an existing AMF config.

No placement, synthesis, server command, or file download is performed.
The input and frozen base are never modified. A new output directory is required.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

CHOICES = {
    "device": ("U250", "VCU108", "VU095", "VHK158"),
    "visualization": ("true", "false"),
    "ProFFClustering": ("true", "false"),
    "ProPrimitiveTiming": ("true", "false"),
    "ProLegacyIOBoundary": ("true", "false"),
    "BoundaryAwareClustering": ("true", "false"),
    "noBlockAware": ("true", "false"),
    "BoundaryClusteringStrategy": ("slr-hierarchical", "cr-slr-hierarchical"),
    "BoundaryStageMode": ("independent-max2", "merged"),
    "BoundaryRegionClipping": ("true", "false"),
    "BoundaryWeakAttraction": ("true", "false"),
    "BoundaryAnchorStopOnRegionEntry": ("true", "false"),
    "ClockRegionColumnStretch": ("true", "false"),
    "IncrementalCompilation": ("true", "false"),
}
KEYS = set(CHOICES) | {"ClockPeriod", "jobs"}
HIERARCHICAL = {
    "BoundaryClusteringStrategy", "BoundaryStageMode",
    "BoundaryRegionClipping", "BoundaryAnchorStopOnRegionEntry",
}
TRACE_KEYS = {
    "VisualizationTraceFine", "DumpAllCoordTrace", "DumpLUTCoordTrace",
    "DumpDSPCoordTrace", "DumpCARRYCoordTrace", "DumpFFCoordTrace",
    "DumpLUTFFCoordTrace", "DumpBRAMCoordTrace",
}
PROFILES = ("78", "83", "099-visualization")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key: " + key)
        result[key] = value
    return result


def read_json(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"),
                      object_pairs_hook=unique_object)
    if not isinstance(data, dict):
        raise ValueError("Expected a JSON object: " + str(path))
    return data


def canonical_device(value):
    return "VCU108" if value == "VU095" else value


def resolve(primary, base, profile):
    if profile not in PROFILES:
        raise ValueError("Unknown source profile; expected " + ", ".join(PROFILES))
    if set(primary) != KEYS:
        raise ValueError("Primary config keys differ: missing=%s unknown=%s" %
                         (sorted(KEYS-set(primary)), sorted(set(primary)-KEYS)))
    if any(not isinstance(v, str) for v in primary.values()):
        raise ValueError('Use AMF string values, for example "true", "10", "12"')
    if any(not isinstance(v, str) for v in base.values()):
        raise ValueError("Base must be a flat AMF JSON, not a flow job JSON")
    for key, choices in CHOICES.items():
        if primary[key] not in choices:
            raise ValueError("%s: expected one of %s" % (key, ", ".join(choices)))
    period = float(primary["ClockPeriod"])
    if not math.isfinite(period) or period <= 0:
        raise ValueError("ClockPeriod must be finite and positive")
    if not primary["jobs"].isdigit() or int(primary["jobs"]) < 1:
        raise ValueError("jobs must be a positive integer")
    if primary["device"] == "VHK158":
        raise ValueError("VHK158 is reserved; no implemented placement policy")
    if canonical_device(primary["device"]) != canonical_device(base.get("device", "VCU108")):
        raise ValueError("device differs from base: choose a base with matching device/model inputs")
    if "vivado extracted device information file" not in base:
        raise ValueError("Base lacks the device input file")
    if not base.get("vivado extracted design information file") and not base.get("Native static cache read"):
        raise ValueError("Base lacks a design netlist or native cache input")

    result = dict(base)
    result.update(primary)
    inactive = {}
    derived = {}
    notes = []
    boundary = primary["BoundaryAwareClustering"] == "true"
    if boundary:
        if not base.get("physical boundary model file"):
            raise ValueError("BoundaryAwareClustering=true requires a physical boundary model in the base")
        result["PhysicalBoundaryMode"] = "true"
        derived["PhysicalBoundaryMode"] = "Required by BoundaryAwareClustering=true"
        inactive["noBlockAware"] = "Only controls the old clock-region clustering path"
        inactive["ClockRegionColumnStretch"] = "New boundary clustering does not execute old stretching"
    else:
        for key in HIERARCHICAL:
            inactive[key] = "BoundaryAwareClustering=false"
        if primary["noBlockAware"] == "true":
            inactive["ClockRegionColumnStretch"] = "Old clustering disabled by noBlockAware=true"
            inactive["BoundaryWeakAttraction"] = "No active old clock-region targets"
        elif canonical_device(primary["device"]) == "U250" and base.get("experimental multi-SLR placement") == "true":
            inactive["BoundaryWeakAttraction"] = "U250 old selected-column attraction does not use this weight switch"
    for key in inactive:
        result.pop(key, None)

    # 78 and the separate visualization copy skip the old stretch call.
    if profile in {"78", "099-visualization"}:
        if canonical_device(primary["device"]) != "U250":
            raise ValueError("Use profile 83 with matching inputs for the restored VU095 device policy")
        if "ClockRegionColumnStretch" in result:
            if result["ClockRegionColumnStretch"] == "true":
                raise ValueError("Profile %s cannot enable stretching; profile 83 implements the switch" % profile)
            result.pop("ClockRegionColumnStretch")
            inactive["ClockRegionColumnStretch"] = "Profile %s already skips stretching; it does not parse this key" % profile
    if primary["ProLegacyIOBoundary"] == "true" and (
            canonical_device(primary["device"]) != "VCU108" or
            result.get("PhysicalBoundaryMode") == "true" or
            result.get("PhysicalBoundaryAudit") == "true"):
        raise ValueError("ProLegacyIOBoundary=true requires VCU108/VU095 without the physical boundary model/audit")
    visual = primary["visualization"] == "true"
    if visual and profile != "099-visualization":
        raise ValueError("visualization=true requires the 099-visualization source profile and its actual binary")
    # The explicit main switch owns all old coordinate trace options. Keep
    # algorithm/placement result exports (e.g. DumpCLBPacking) untouched.
    cleared = sorted(key for key in TRACE_KEYS if key in result)
    for key in TRACE_KEYS:
        result.pop(key, None)
    if cleared:
        derived["coordinate_trace_cleanup"] = {
            "reason": "Explicit visualization switch overrides legacy trace options",
            "removed_keys": cleared,
        }
    if profile != "099-visualization":
        result.pop("visualization")
        inactive["visualization"] = "False: trace options cleared without injecting an unsupported key"
    elif visual:
        notes.append("Use the 099 visualization binary; it will write dumpDirectory/visualization. No viewer service is launched.")
    incremental = primary["IncrementalCompilation"] == "true"
    old_incremental = base.get("IncremenralCompilation")
    if old_incremental is not None and "IncrementalCompilation" in base and old_incremental != base["IncrementalCompilation"]:
        raise ValueError("Base has conflicting canonical and legacy incremental keys")
    result.pop("IncremenralCompilation", None)
    dfx_input = any(k.startswith("DFX ") or k.startswith("Native ") for k in base)
    if not incremental and dfx_input:
        raise ValueError("This is a DFX/native base; choose an ordinary full base to disable incremental context")
    if incremental:
        mode = base.get("DFX placement mode", "incremental")
        if mode == "full":
            required = ("DFX regions file", "DFX region sites file")
        elif mode == "incremental" and base.get("Native static cache read"):
            required = ("Native static cache read", "Native static identity", "Native static shell sha256",
                        "Native active module", "Native module netlist", "Native module ports file", "Native workspace")
        elif mode == "incremental":
            required = ("DFX active partition", "DFX allowed sites file",
                        "DFX blocked sites file", "DFX static placement file")
        else:
            raise ValueError("Invalid DFX placement mode in base")
        missing = [key for key in required if not base.get(key)]
        if missing:
            raise ValueError("Incremental/DFX enabled but matching context is missing: " + ", ".join(missing))
        if mode == "full" and base.get("Native static cache read"):
            raise ValueError("Native cache read would override full DFX mode")
        result["DFX placement mode"] = mode
        if mode == "incremental" and result["ClockPeriod"] != base.get("ClockPeriod"):
            raise ValueError("Do not change the clock of an inherited static context in a placement-only overlay")
        notes.append("DFX input/cache identities still require the original flow's certification; no flow job is generated.")
    if result.get("BipartiteMatchingBackend", "legacy") == "legacy" and primary["jobs"] != base.get("jobs", "1"):
        notes.append("legacy matching can change task grouping/layout when jobs changes.")
    if result["ClockPeriod"] != base.get("ClockPeriod"):
        notes.append("Only the AMF target changed. Vivado XDC and per-driver ClockPeriod overrides were not changed.")
    notes.append("SllRegionPolicy is not parsed by these profiles; no SLL options are injected.")
    notes.append("Source profile is declared, not a live binary/remote input verification.")
    changes = {k: {"before": base.get(k), "after": result.get(k)}
               for k in sorted(set(base) | set(result)) if base.get(k) != result.get(k)}
    return result, dict(profile=profile, inactive=inactive, derived=derived, changes=changes, notes=notes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, help="The common-settings user config")
    parser.add_argument("--base", required=True, help="Matching full flat AMF config, never modified")
    parser.add_argument("--profile", choices=PROFILES, default="78",
                        help="78=82/84; 83=device policy; 099-visualization=separate visualization copy")
    parser.add_argument("--output", help="New directory for effective config/report; omit for check only")
    args = parser.parse_args()
    config_path = Path(args.config).resolve()
    base_path = Path(args.base).resolve()
    primary, base = read_json(config_path), read_json(base_path)
    result, report = resolve(primary, base, args.profile)
    report.update(config_path=str(config_path), base_path=str(base_path),
                  config_sha256=hashlib.sha256(config_path.read_bytes()).hexdigest(),
                  base_sha256=hashlib.sha256(base_path.read_bytes()).hexdigest(),
                  experiment_started=False)
    if args.output:
        out = Path(args.output).resolve()
        # Avoid carrying old writable output locations into a new configuration.
        outputs = {
            "dumpDirectory": "placement", "BoundaryReportDirectory": "reports/physical",
            "DFX boundary audit file": "reports/dfx-boundary.tsv",
            "DFX effective sites file": "reports/dfx-effective-sites.tsv",
            "DFX partition audit file": "reports/dfx-partition.tsv",
            "Native workspace": "native", "Native static cache write": "native/proposed.bin",
        }
        unmanaged = [k for k in result if k.startswith("Dump") and Path(result[k]).is_absolute()]
        if unmanaged:
            raise ValueError("Base has absolute diagnostic output prefixes; choose fresh relative prefixes first: " +
                             ", ".join(unmanaged))
        for key in ("SllReportDirectory", "Native static cache sealed output"):
            if key in result:
                raise ValueError("Base contains output/action fields outside this placement-only exporter: " + key)
        report["output_relocations"] = {}
        for key, suffix in outputs.items():
            if key in result or key == "dumpDirectory":
                value = str(out/suffix)
                report["output_relocations"][key] = {"before": result.get(key), "after": value}
                result[key] = value
        out.mkdir(parents=True, exist_ok=False)
        for key in ("dumpDirectory", "BoundaryReportDirectory", "Native workspace"):
            if key in result:
                Path(result[key]).mkdir(parents=True, exist_ok=True)
        (out/"reports").mkdir(exist_ok=True)
        (out/"amf_config.json").write_text(json.dumps(result, indent=2, ensure_ascii=False)+"\n")
        (out/"resolution.json").write_text(json.dumps(report, indent=2, ensure_ascii=False)+"\n")
        print("Generated: " + str(out/"amf_config.json"))
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit("Configuration error: " + str(error))
