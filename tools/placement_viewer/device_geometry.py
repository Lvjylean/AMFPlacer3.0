"""Placement-coordinate geometry, bound to the input device rather than a UI label."""
import hashlib
import json
import math
from pathlib import Path

SUPPORTED_DEVICES = {"U250": "xcu250-", "VHK158": "xcvh1582-"}


def identity(config):
    device = config.get("device", "")
    part = config.get("physical device part", "").lower()
    if device not in SUPPORTED_DEVICES:
        raise ValueError("Viewer device must be U250 or VHK158")
    if not part.startswith(SUPPORTED_DEVICES[device]):
        raise ValueError("Device label does not match the part: " + device + " / " + part)
    return {"device": device, "part": part}


def rectangle(value):
    if (len(value) != 4 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in value)
            or value[0] >= value[1] or value[2] >= value[3]):
        raise ValueError("Invalid device geometry rectangle")
    return list(value)


def load_explicit(config, config_path):
    path = Path(config["visualization device metadata file"])
    if not path.is_absolute():
        path = config_path.parent / path
    geometry = json.loads(path.read_text())
    ident = identity(config)
    if geometry.get("schema") != 1 or any(geometry.get(k) != v for k, v in ident.items()):
        raise ValueError("Visualization geometry identity/schema mismatch")
    source = Path(config["vivado extracted device information file"])
    if geometry.get("device_input_sha256") != hashlib.sha256(source.read_bytes()).hexdigest():
        raise ValueError("Visualization geometry belongs to a different device input")
    rectangle(geometry["bounds"])
    for key in ("clock_regions", "slr_regions"):
        names = set()
        if not geometry.get(key):
            raise ValueError("Empty geometry: " + key)
        for region in geometry[key]:
            rectangle(region["bounds"])
            if region["name"] in names:
                raise ValueError("Duplicate region name")
            names.add(region["name"])
    stretch = geometry.get("display_x_stretch", 1)
    if not isinstance(stretch, (int, float)) or not 0 < stretch <= 10:
        raise ValueError("Invalid display scale")
    return geometry


def legacy_geometry(config, bounds, xb, yb, seams):
    ident = identity(config)
    if ident["device"] != "U250":
        raise ValueError("VHK158 requires explicit, device-bound visualization geometry")
    ys = [bounds[2], *seams, bounds[3]]
    return dict(**ident, bounds=bounds, cr_x=xb, cr_y=yb, slr=seams,
                display_x_stretch=4,
                clock_regions=[dict(name=f"X{x}Y{y}", bounds=[xb[x], xb[x+1], yb[y], yb[y+1]])
                               for x in range(len(xb)-1) for y in range(len(yb)-1)],
                slr_regions=[dict(name=f"SLR{i}", bounds=[bounds[0], bounds[1], ys[i], ys[i+1]])
                             for i in range(len(ys)-1)])
