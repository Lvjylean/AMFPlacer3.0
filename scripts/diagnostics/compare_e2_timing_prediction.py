"""Read-only reconstruction of E2 timing predictions at saved final placement."""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import time
import zipfile


def rows(path):
    with path.open() as stream:
        yield from csv.DictReader(stream, delimiter="\t")


def write_rows(path, data):
    if not data:
        return
    with path.open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(dict.fromkeys(k for row in data for k in row)), delimiter="\t")
        writer.writeheader()
        writer.writerows(data)


def stats(data):
    if not data:
        return {"n": 0}
    data = sorted(data)
    return {"n": len(data), "mean": statistics.mean(data),
            "median": statistics.median(data), "min": data[0], "max": data[-1],
            "p90": data[min(len(data)-1, math.ceil(.9*len(data))-1)]}


parser = argparse.ArgumentParser()
parser.add_argument("--run", type=Path, required=True)
parser.add_argument("--comparison", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
started = time.time()
out = args.output
out.mkdir(parents=True, exist_ok=False)
(out / "reports").mkdir()
(out / "inputs").mkdir()
(out / "inputs/analysis_source.py").write_bytes(Path(__file__).read_bytes())
(out / "status.json").write_text(json.dumps({"state": "running"})+"\n")
run = args.run
comp = args.comparison / "reports"
cfg = json.loads((run / "config.json").read_text())
manifest = json.loads((run / "manifest.json").read_text())
build = json.loads(Path(manifest["algorithm_build_manifest"]).read_text())
snapshot = Path(build["source_snapshot"])
samples = []
for mode in ["fixed", "free"]:
    samples.extend(dict(row, mode=mode, sample="top100")
                   for row in rows(comp / mode / "timing_connections.tsv"))
for row in rows(comp / "paired-critical-connections.tsv"):
    samples.append(dict(row, path=row["pair_id"], sample="paired20",
                        routed_delay_ns=row["slow_max_ns"]))
wanted = {(r["source_pin"], r["sink_pin"]) for r in samples}
critical = list(rows(run / "reports/physical/amf_critical_paths.tsv"))
final = [r for r in critical if r["stage"] == "amf-final-packed"]
final_ids = {int(r["endpoint"]) for r in final}

# The final printed predecessor chain is independent evidence for replay QA.
log = (run / "logs/amf.log").read_text()
tail = log.rsplit("An example of long delay path for the current placement:", 1)[1]
chain = []
for line in tail.splitlines():
    match = re.search(r"DesignCell: \((\S+)\) (\S+).*? X:([-\d.]+) Y:([-\d.]+)", line)
    if match:
        delay = re.search(r"netDelay:([-\d.]+)", line)
        chain.append(dict(type=match[1], cell=match[2], x=float(match[3]),
                          y=float(match[4]), logged_outgoing_delay_ns=float(delay[1]) if delay else None))
chain.reverse()
chain_pairs = {(a["cell"], b["cell"]): a["logged_outgoing_delay_ns"]
               for a, b in zip(chain, chain[1:])}
cells_wanted = {pin.rsplit("/", 1)[0] for pair in wanted for pin in pair}
cells_wanted.update(r["cell"] for r in chain)
names_by_id = {}
cell_ids = {}
cell_types = {}
chain_connections = []
seen = set()
cell_id = -1
netlist = Path(cfg["vivado extracted design information file"])
with zipfile.ZipFile(netlist) as archive, archive.open(archive.namelist()[0]) as stream:
    target = False
    for raw in stream:
        if raw.startswith(b"curCell=> "):
            fields = raw.decode().split()
            cell, typ = fields[1], fields[3]
            if cell not in seen:
                seen.add(cell)
                cell_id += 1
            else:
                raise RuntimeError("Unexpected duplicate input cell: " + cell)
            target = cell in cells_wanted
            if target or cell_id in final_ids:
                names_by_id[cell_id] = cell
                cell_ids[cell] = cell_id
                cell_types[cell] = typ
        elif target and b"drivepin=>" in raw:
            fields = raw.decode().split()
            pin = fields[1]
            drivers = fields[fields.index("drivepin=>")+1:]
            if drivers and "/" in drivers[0]:
                source = drivers[0]
                pair = (source.rsplit("/", 1)[0], cell)
                if pair in chain_pairs:
                    wanted.add((source, pin))
                    chain_connections.append(dict(source_pin=source, sink_pin=pin,
                        logged_delay_ns=chain_pairs[pair]))
print("parsed netlist", len(seen), "selected connections", len(wanted), flush=True)
assert names_by_id[int(final[0]["endpoint"])] == chain[-1]["cell"]
cells_wanted.update(names_by_id.values())

cell_sites = {}
with (run / "placement/requested.tsv").open() as stream:
    for line in stream:
        fields = line.rstrip("\n").split("\t")
        if fields[0] in cells_wanted:
            cell_sites[fields[0]] = fields[1].split("/", 1)[0]
site_names = set(cell_sites.values())
sites = {}
cuts = []
with Path(cfg["physical boundary model file"]).open() as stream:
    for line in stream:
        f = line.rstrip("\n").split("\t")
        if f[0] == "SITE" and f[1] in site_names:
            sites[f[1]] = (float(f[3]), float(f[4]), int(f[5]))
        elif f[0] == "BOUNDARY" and f[8] == "1":
            cuts.append(dict(kind=f[2], axis=f[3], at=float(f[4]), low=float(f[5]),
                             high=float(f[6]), penalty=float(f[7])))
positions = {c: sites[s] for c, s in cell_sites.items()}
coordinate_check = [dict(cell=r["cell"], logged=[r["x"], r["y"]],
                         final=list(positions[r["cell"]][:2])) for r in chain]

channels = {}
tile_max = collections.defaultdict(float)
with Path(cfg["SllChannelFile"]).open() as stream:
    for row in csv.DictReader((l for l in stream if not l.startswith("#")), delimiter="\t"):
        for k in ["in_x", "in_y", "out_x", "out_y", "slow_max"]:
            row[k] = float(row[k])
        row["from"] = int(row["from"])
        row["to"] = int(row["to"])
        row["tile_key"] = (row["from"], row["to"], row["in_x"], row["in_y"], row["out_x"], row["out_y"])
        channels[(row["resource"], row["from"])] = row
        tile_max[row["tile_key"]] = max(tile_max[row["tile_key"]], row["slow_max"])
with (out / "inputs/selected-pins.tsv").open("w") as stream:
    for source, sink in sorted(wanted):
        stream.write(source + "\t" + sink + "\n")
tcl = r'''
set f [open [lindex $argv 0] r]
set wanted [dict create]
while {[gets $f line] >= 0} {
    lassign [split $line "\t"] driver sink
    dict set wanted $driver $sink 1
}
close $f
source [lindex $argv 1]
set out [open [lindex $argv 2] w]
puts $out "source_pin\tsink_pin\tchannels"
proc visit {driver tree path} {
    global wanted out
    foreach child $tree {
        if {[dict exists $child pin]} {
            set sink [dict get $child pin]
            if {[dict exists $wanted $driver $sink]} {
                puts $out "$driver\t$sink\t[join $path ,]"
            }
        } else {
            set next $path
            lappend next "[dict get $child from]|[dict get $child resource]"
            visit $driver [dict get $child children] $next
        }
    }
}
foreach plan $e2_sll_plan {
    set driver [dict get $plan driver]
    if {[dict exists $wanted $driver]} {visit $driver [dict get $plan tree] {}}
}
close $out
'''
(out / "inputs/extract_selected_sll.tcl").write_text(tcl)
subprocess.run(["tclsh", str(out / "inputs/extract_selected_sll.tcl"),
                str(out / "inputs/selected-pins.tsv"), str(run / "placement/sll-plan.tcl"),
                str(out / "reports/selected-sll-plan.tsv")], check=True)
plans = {}
for row in rows(out / "reports/selected-sll-plan.tsv"):
    sequence = []
    for item in filter(None, row["channels"].split(",")):
        direction, name = item.split("|", 1)
        sequence.append(channels[(name, int(direction))])
    plans[(row["source_pin"], row["sink_pin"])] = sequence


def local(a, b):
    x, y = abs(a[0]-b[0]), abs(a[1]-b[1])
    if x*x + y*y < 9:
        c = (95.05263521, -26.50563359, 77.42394117, 106.29195883, -14.975527)
    elif x*x + y*y < 36:
        c = (160.1553292055571, -64.94911625084538, 22.807442252015385, 120.36632967195294, 27.120924804902547)
    else:
        c = (202.12777470189823, -146.96155453685554, -131.59702340956898, 163.46881911227194, 136.17523346378107)
    x *= 2
    value = max(.05, (c[0] + x**.3*c[1] + y**.3*c[2] + x**.5*c[3] + y**.5*c[4])/1000)
    for cut in cuts:
        if cut["kind"] == "SLR":
            continue
        k = 0 if cut["axis"] == "X" else 1
        if (min(a[k], b[k]) < cut["at"] < max(a[k], b[k])
                and cut["low"] <= a[1-k] <= cut["high"]
                and cut["low"] <= b[1-k] <= cut["high"]):
            value += cut["penalty"]
    return value


def predict(source, sink, sequence=None):
    a, b = positions[source.rsplit("/", 1)[0]], positions[sink.rsplit("/", 1)[0]]
    if sequence is None:
        sequence = plans.get((source, sink), [])
        if a[2] != b[2] and not sequence:
            raise RuntimeError("Missing cross-SLR plan: " + source + " -> " + sink)
    cur = a
    local_parts = []
    intrinsic = []
    lane = []
    for ch in sequence:
        local_parts.append(local(cur, (ch["in_x"], ch["in_y"])))
        intrinsic.append(tile_max[ch["tile_key"]])
        lane.append(ch["slow_max"])
        cur = (ch["out_x"], ch["out_y"])
    local_parts.append(local(cur, b))
    return dict(predicted_net_ns=sum(local_parts)+sum(intrinsic),
                local_model_ns=sum(local_parts), sll_envelope_ns=sum(intrinsic),
                sll_lane_ns=sum(lane), local_segments_ns=local_parts,
                source_slr=a[2], sink_slr=b[2],
                planned_columns=",".join(ch["column"] for ch in sequence))


evaluated = []
for row in samples:
    d = predict(row["source_pin"], row["sink_pin"])
    actual = float(row["routed_delay_ns"])
    evaluated.append(dict(row, **d, actual_minus_prediction_ns=actual-d["predicted_net_ns"],
                          relative_underestimate=(actual-d["predicted_net_ns"])/actual if actual else 0))
write_rows(out / "reports/connection-predictions.tsv", evaluated)
chain_replay = []
for row in chain_connections:
    d = predict(row["source_pin"], row["sink_pin"])
    chain_replay.append(dict(row, **d, replay_minus_logged_ns=d["predicted_net_ns"]-row["logged_delay_ns"]))
write_rows(out / "reports/amf-worst-replay.tsv", chain_replay)

final_by_cell = {names_by_id[int(r["endpoint"])]: r for r in final}
endpoint_checks = []
for mode in ["fixed", "free"]:
    for row in rows(comp / mode / "timing_paths.tsv"):
        endpoint = row["endpoint"].rsplit("/", 1)[0]
        match = final_by_cell.get(endpoint)
        endpoint_checks.append(dict(mode=mode, **row, endpoint_cell=endpoint,
            amf_top200_cell_rank=match["rank"] if match else "",
            amf_cell_slack_ns=match["slack_ns"] if match else ""))
write_rows(out / "reports/endpoint-criticality-comparison.tsv", endpoint_checks)
groups = {}
for mode in ["fixed", "free"]:
    unique = {(r["source_pin"], r["sink_pin"]): r for r in evaluated
              if r["mode"] == mode and r["sample"] == "top100"}
    for kind in ["all", "cross_die", "same_die"]:
        selected = [r for r in unique.values() if kind == "all" or
                    (r["source_slr"] != r["sink_slr"]) == (kind == "cross_die")]
        errors = [r["actual_minus_prediction_ns"] for r in selected]
        groups[mode+"_"+kind] = dict(signed_actual_minus_predicted_ns=stats(errors),
            absolute_error_ns=stats([abs(e) for e in errors]),
            rmse_ns=math.sqrt(statistics.mean(e*e for e in errors)) if errors else None,
            predicted_ns=stats([r["predicted_net_ns"] for r in selected]),
            actual_ns=stats([float(r["routed_delay_ns"]) for r in selected]))

summary = dict(schema="e2-final-placement-timing-prediction-audit-v1",
    scope="Offline reconstruction using frozen build formula, saved final cell sites and exported SLL tile plan; not a new placement or routing run. Top100 samples are critical-path biased, not whole-design fit statistics.",
    amf_run=str(run), comparison=str(args.comparison), snapshot=str(snapshot),
    source_commit=manifest["source_commit"], binary_sha256=manifest["binary_sha256"],
    coordinates_check_against_final_logged_path=coordinate_check,
    amf_final_worst=final[0], amf_final_worst_chain=chain,
    replay_validation=chain_replay, groups=groups,
    routed_top100_endpoint_cells_in_amf_top200={mode: sum(bool(r["amf_top200_cell_rank"])
        for r in endpoint_checks if r["mode"] == mode) for mode in ["fixed", "free"]},
    routed_worst_endpoint_checks=[r for r in endpoint_checks if r["path"] == "0"],
    representative_connections=[r for r in evaluated if r["sample"] == "paired20" and r["path"] in ["0", "10"]],
    tile_sll_envelope_range_ns=stats(list(tile_max.values())),
    elapsed_seconds=time.time()-started,
    limitations=["AMF audit ranks endpoint cells, while Vivado ranks pin-level paths; a matched endpoint does not prove the same startpoint or input pin.",
        "No per-edge final STA dump was enabled; reconstructed predictions are checked against the final printed worst chain.",
        "Model local pin offsets are zero for the sampled FDRE/LUT/BRAM/URAM types; selected types are recorded.",
        "Free routing may choose a different SLL than planned, so its residual includes route-choice divergence."])
(out / "reports/summary.json").write_text(json.dumps(summary, indent=2)+"\n")
provenance = {"source_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "frozen_model_sha256": {str(p.relative_to(snapshot)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in snapshot.rglob("*") if p.name in ["SllPlanner.cc", "SllVirtualModel.h", "SllTilePlacement.h", "PlacementTimingOptimizer.h", "PlacementTimingOptimizer.cc", "PlacementTimingInfo.cc"]},
    "config_sha256": manifest["config_sha256"], "sll_plan_sha256": manifest["sll_plan_sha256"],
    "netlist_sha256": manifest["inputs"]["vivado extracted design information file"]["sha256"],
    "sample_cell_types": sorted(set(cell_types.get(pin.rsplit("/", 1)[0], "MISSING") for pair in wanted for pin in pair)),
    "amf_log_sha256": hashlib.sha256((run/"logs/amf.log").read_bytes()).hexdigest()}
(out / "manifest.json").write_text(json.dumps(provenance, indent=2)+"\n")
(out / "status.json").write_text(json.dumps({"state": "completed", "elapsed_seconds": time.time()-started})+"\n")
print(json.dumps({"output": str(out), "groups": groups,
                  "endpoint_matches": summary["routed_top100_endpoint_cells_in_amf_top200"],
                  "replay_validation": chain_replay,
                  "representative_connections": summary["representative_connections"]}, indent=2))
