#!/usr/bin/env python3
"""Watch one full R10 run and reconcile profiling, QoR and routed SLL reports."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import time
from summarize_runtime_profile import summarize

def now():
    return datetime.now().astimezone().isoformat()

def read_json(path):
    for attempt in range(5):
        try:
            return json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            if attempt == 4:
                raise
            time.sleep(0.2)

def save(path, value):
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    tmp.replace(path)

def read_qor(run, amf_source=None):
    reports = run / "reports"
    summary = read_json(reports / "summary.json")
    cfg = read_json(run / "config.json")
    log = ((amf_source or run) / "logs/amf.log").read_text(errors="replace")
    hpwl = re.findall(r"Current Total HPWL\s*=\s*([0-9.eE+-]+)", log)
    if not hpwl:
        raise ValueError("Final AMF HPWL marker missing")
    utilization = (reports / "utilization.rpt").read_text()
    boundaries, directions, totals = [], [], []
    for line in utilization.splitlines():
        fields = [x.strip() for x in line.split("|")[1:-1]]
        if len(fields) < 2:
            continue
        label = fields[0]
        if re.fullmatch(r"SLR\d+ <-> SLR\d+", label):
            boundaries.append(dict(boundary=label, used=int(fields[1].replace(",", "")),
                available=int(fields[3].replace(",", "")), utilization_percent=float(fields[4])))
        elif re.fullmatch(r"SLR\d+ -> SLR\d+", label):
            directions.append(dict(direction=label, used=int(fields[1].replace(",", ""))))
        elif label == "Total SLLs Used":
            totals.append(int(fields[1].replace(",", "")))
    if len(totals) != 1 or not boundaries or sum(x["used"] for x in boundaries) != totals[0]:
        raise ValueError("Native SLL total and per-boundary usage do not reconcile")
    return dict(amf_final_weighted_hpwl=float(hpwl[-1]),
        hpwl_y2x_ratio=float(cfg["y2xRatio"]),
        hpwl_coordinate_file=cfg["vivado extracted device information file"],
        hpwl_definition="Final Current Total HPWL from AMF; coordinate/ratio dependent, not routed wirelength.",
        timing=summary["timing"], total_slls_used=totals[0],
        sll_boundaries=boundaries, sll_directions=directions,
        sll_source=str(reports / "utilization.rpt"),
        routing_complete=summary["routing_complete"],
        strict_import_verified=summary["strict_import_verified"],
        implementation_verified=summary["implementation_verified"],
        drc_counts=summary["drc_counts"],
        sll_definition="Vivado post-route Total SLLs Used; summed boundary resource uses, not unique cross-SLR net count.")

def sample_processes(run):
    rows = []
    for p in Path("/proc").iterdir():
        if not p.name.isdigit():
            continue
        try:
            if p.stat().st_uid != os.getuid():
                continue
            cmd = (p/"cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
            if str(run) not in cmd or not any(x in cmd for x in ("AMFPlacer", "vivado", "loader")):
                continue
            text = (p/"stat").read_text()
            fields = text[text.rfind(")")+2:].split()
            rows.append(dict(pid=int(p.name), state=fields[0],
                cpu_s=(int(fields[11])+int(fields[12]))/os.sysconf("SC_CLK_TCK"),
                rss_kib=int(fields[21])*os.sysconf("SC_PAGE_SIZE")/1024))
        except (OSError, ValueError):
            continue
    return rows

def finish(run, profile):
    manifest = read_json(run/"manifest.json")
    status = read_json(run/"status.json")
    qor = read_qor(run)
    stages = {x["name"]:x["elapsed_seconds"] for x in manifest["stages"]}
    vivado = {}
    with (run/"reports/stages.tsv").open() as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            vivado[row["stage"]] = float(row["seconds"])
    elapsed = (datetime.fromisoformat(status["finished"]) -
               datetime.fromisoformat(manifest["started"])).total_seconds()
    accounting = dict(
        amf_placement_functional_wall_s=profile["placement_functional_wall_s"],
        amf_process_wall_s=profile["amf_process_wall_s"],
        amf_non_placement_categories_s=profile["non_placement_categories_s"],
        amf_outside_main_and_profile_flush_s=profile["outside_main_and_profile_flush_s"],
        amf_to_vivado_export_s=profile["non_placement_categories_s"].get("export_vivado", 0),
        amf_to_vivado_adapter_s=stages.get("amf_to_vivado_adapter"),
        vivado_import_s=vivado["import"], vivado_placement_s=vivado["place"],
        vivado_routing_s=vivado["route"],
        vivado_stages_s=vivado, full_flow_wall_s=elapsed,
        dcp_to_amf_export=dict(executed=False, cached_input=True, this_run_seconds=0,
            first_export_cost="Not measured in this run"),
        caveats=["AMF process and functional times are nested, not additive.",
            "Parallel worker wall times overlap; function self times avoid double counting.",
            "Profiling overhead and shared-host load are retained in measurements.",
            "Do not compare raw R09/R10 HPWL improvement percentages across different coordinates and ratios."])
    result = dict(label="R10", run_id=run.name, created=now(), qor=qor,
        timing_accounting=accounting, profile_summary=str(run/"reports/profile_summary.json"),
        final_dcp=str(run/"reports/getrf_routed.dcp"),
        final_dcp_sha256=status.get("output_dcp_sha256"))
    save(run/"reports/r10_metrics.json", result)
    lines = ["# R10 完整实验结果", "", "运行：" + str(run), "",
        "| 指标 | 结果 |", "|---|---:|",
        "| AMF 最终加权 HPWL | " + str(qor["amf_final_weighted_hpwl"]) + " |",
        "| Routed WNS (ns) | " + str(qor["timing"]["wns_ns"]) + " |",
        "| Total SLLs Used | " + str(qor["total_slls_used"]) + " |",
        "| AMF 实际布局（分钟） | %.3f |" % (accounting["amf_placement_functional_wall_s"]/60),
        "| AMF 进程墙钟（分钟） | %.3f |" % (accounting["amf_process_wall_s"]/60),
        "| Vivado placement（分钟） | %.3f |" % (vivado["place"]/60),
        "| Vivado routing（分钟） | %.3f |" % (vivado["route"]/60),
        "| 全流程墙钟（分钟） | %.3f |" % (elapsed/60),
        "", "适配、检查、报告、DCP 写出详见 r10_metrics.json；各功能耗时详见 profile_summary.md 和 profile_functions.csv。",
        "", "HPWL 使用本轮坐标与权重，不能与 R09 原始数值直接计算改善率。SLL 使用 Vivado 布线后资源统计。",
        "", "最终 DCP（仅服务器）：" + result["final_dcp"], ""]
    (run/"reports/r10_summary.md").write_text("\n".join(lines))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--check-inputs", action="store_true")
    args = parser.parse_args()
    run = args.run.resolve()
    if args.check_inputs:
        print(json.dumps(read_qor(run), ensure_ascii=False, indent=2))
        return
    for name in ("finish_r10_full_flow.py", "summarize_runtime_profile.py"):
        shutil.copy2(Path(__file__).with_name(name), run/"inputs"/name)
    target = run/"reports/r10_supervisor.json"
    outcome = dict(state="monitoring", started=now(), pid=os.getpid(),
        collector_sha256={n:hashlib.sha256((run/"inputs"/n).read_bytes()).hexdigest()
            for n in ("finish_r10_full_flow.py", "summarize_runtime_profile.py")})
    save(target, outcome)
    profile = None
    try:
        with (run/"reports/profile_resource_samples.jsonl").open("a") as stream:
            while True:
                status = read_json(run/"status.json")
                manifest = read_json(run/"manifest.json")
                sample = dict(time=now(), status=status,
                    load_average=Path("/proc/loadavg").read_text().strip(),
                    processes=sample_processes(run))
                stream.write(json.dumps(sample)+"\n")
                stream.flush()
                amf_done = any(s["name"]=="amf" and s["exit_code"]==0 for s in manifest["stages"])
                if profile is None and amf_done:
                    profile = summarize(run)
                    outcome.update(amf_profile_completed=now(), state="waiting-for-backend")
                    save(target, outcome)
                if status["state"] != "running":
                    break
                time.sleep(15)
        if status["state"] != "completed":
            raise RuntimeError("Full flow failed; preserve available AMF profile: "+str(status))
        if profile is None:
            raise RuntimeError("Successful AMF profile is unavailable")
        finish(run, profile)
        outcome.update(state="completed", finished=now(), metrics=str(run/"reports/r10_metrics.json"))
    except Exception as error:
        outcome.update(state="failed", finished=now(), error=str(error))
        raise
    finally:
        save(target, outcome)

if __name__ == "__main__":
    main()
