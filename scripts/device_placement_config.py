"""Protect explicit device-policy options when selecting a frozen AMF binary."""
import json
import subprocess

def validate_device_placement_binary(config, binary):
    device = config.get("device", "VCU108")
    if device == "VHK158":
        raise ValueError("device=VHK158 placement policy is reserved but not implemented")
    if "ClockRegionColumnStretch" in config:
        if config["ClockRegionColumnStretch"] not in ("true", "false"):
            raise ValueError("ClockRegionColumnStretch must be true or false")
    # Preserve historical runs without the new option. VU095 is a new alias;
    # legacy VCU108 and U250 remain usable with their frozen old binaries.
    if "ClockRegionColumnStretch" not in config and device != "VU095":
        return
    result = subprocess.run([str(binary), "--capabilities"], capture_output=True, text=True, timeout=15)
    try:
        cap = json.loads(result.stdout)
        valid = result.returncode == 0 and cap.get("device_placement_policy_schema") == 1
    except (ValueError, AttributeError):
        cap, valid = {}, False
    if not valid or ("ClockRegionColumnStretch" in config and cap.get("clock_region_column_stretch_schema") != 1):
        raise ValueError("Binary does not support device placement policy / ClockRegionColumnStretch; refusing silent fallback")
