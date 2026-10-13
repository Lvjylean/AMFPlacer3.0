#!/usr/bin/env python3
"""Compare the recorded same-binary matching/candidate ablation; no run mutation."""
import argparse
import datetime as dt
import hashlib
import json
import re
from pathlib import Path


ANSI = re.compile(r"\x1b\[[0-9;]*m")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def trace(path):
    lines = ANSI.sub("", path.read_text(errors="replace")).splitlines()
    hpwl = [re.sub(r" \(elapsed time:.*", "", line)
            for line in lines if "HPWL" in line and "====" not in line]
    return lines, hpwl


def compare(reference, observed):
    for index, (left, right) in enumerate(zip(reference, observed)):
        if left != right:
            return dict(common_prefix=index, first_difference=dict(
                record=index, reference=left, observed=right))
    return dict(common_prefix=min(len(reference), len(observed)),
                first_difference=None, reference_records=len(reference),
                observed_records=len(observed), equal_complete_sequences=reference == observed)


def analyze(root, evidence):
    manifest = json.loads((evidence / "manifest.json").read_text())
    historical = root / "experiments/runs/getrf-u250-full-20260928-190947-261685"
    failed = root / "experiments/runs/getrf-u250-full-20260929-012436-908489"
    _, historical_trace = trace(historical / "logs/amf.log")
    _, failed_trace = trace(failed / "logs/amf.log")
    result = dict(checked=dt.datetime.now().astimezone().isoformat(),
                  binary=manifest["binary"], binary_sha256=manifest["binary_sha256"],
                  timing_scope="Concurrent AMF process wall time; not isolated speed or pure placement time",
                  runs=[])
    sequences = {}
    controls = None
    for item in manifest["runs"]:
        launch = Path(item["log"]).read_text().splitlines()
        row = dict(label=item["label"], backend=item["backend"],
                   candidate_selection=item["candidate_selection"])
        result["runs"].append(row)
        if not launch or not Path(launch[0]).is_dir():
            row["state"] = "launcher_pending_or_failed"
            row["launcher"] = launch
            continue
        run = Path(launch[0])
        row["run"] = str(run)
        status_path = run / "status.json"
        row["status"] = json.loads(status_path.read_text()) if status_path.exists() else {}
        mf = json.loads((run / "manifest.json").read_text())
        row["same_binary"] = mf["binary_sha256"] == manifest["binary_sha256"]
        cfg = json.loads((run / "config.json").read_text())
        excluded = {"BipartiteMatchingBackend", "MacroCandidateSelection",
                    "dumpDirectory", "BoundaryReportDirectory"}
        control = dict(config={k: v for k, v in cfg.items() if k not in excluded},
                       input_hashes={k: v["sha256"] for k, v in mf["inputs"].items()},
                       dcp_sha256=mf["input_dcp_sha256"],
                       runtime_profiling=mf["runtime_profiling"])
        if controls is None:
            controls = control
        row["same_controlled_inputs_and_parameters"] = control == controls
        row["stages"] = mf["stages"]
        lines, sequence = trace(run / "logs/amf.log")
        sequences[item["label"]] = sequence
        row["hpwl_records"] = len(sequence)
        row["versus_historical"] = compare(historical_trace, sequence)
        row["versus_failed_fast"] = compare(failed_trace, sequence)
        row["latest_status"] = next((line for line in reversed(lines) if "STATUS:" in line), "")
        finals = [float(m.group(1)) for line in lines for m in
                  [re.search(r"Current Total HPWL = ([0-9.]+)", line)] if m]
        row["latest_total_hpwl"] = finals[-1] if finals else None
        matcher_lines = [line for line in lines if "AMF_MATCHER backend=" in line]
        row["matcher_calls"] = len(matcher_lines)
        row["graphs"] = []
        for index in range(min(12, len(matcher_lines))):
            graph = Path(item["graph_dump"]) / f"matching-{index}.txt"
            if graph.exists():
                row["graphs"].append(dict(index=index, sha256=digest(graph),
                    header=graph.open().readline().strip()))
        placement = run / "placement/DumpCLBPacking-first-0.tcl"
        if row["status"].get("state") == "completed" and placement.exists():
            row["placement_sha256"] = digest(placement)
            row["placement_matches_historical"] = row["placement_sha256"] == digest(
                historical / "placement/DumpCLBPacking-first-0.tcl")
            row["placement_matches_failed_fast"] = row["placement_sha256"] == digest(
                failed / "placement/DumpCLBPacking-first-0.tcl")
    result["pairwise"] = {f"{a} vs {b}": compare(sequences[a], sequences[b])
        for a, b in [("a-legacy-legacy", "b-assignment-legacy"),
                     ("a-legacy-legacy", "c-legacy-cached"),
                     ("b-assignment-legacy", "d-assignment-cached")]
        if a in sequences and b in sequences}
    result["all_finished"] = all(row.get("status", {}).get("state") in
                                 {"completed", "failed", "stopped", "cancelled"}
                                 for row in result["runs"])
    (evidence / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = ["# GETRF 同构建匹配／候选排序对照", "", "更新时间：" + result["checked"], "",
             "冻结二进制 SHA256：`" + result["binary_sha256"] + "`。四组并行，仅执行 AMF；耗时不是隔离负载的性能比较。", "",
             "| 组 | 求解器 | 候选排序 | 状态 | 已记录 HPWL 条数 | 最终 HPWL |", "|---|---|---|---|---:|---:|"]
    for row in result["runs"]:
        state = row.get("status", {}).get("state", row.get("state", "unknown"))
        final = row.get("latest_total_hpwl") if state == "completed" else None
        lines.append(f"| {row['label']} | {row['backend']} | {row['candidate_selection']} | {state} | "
                     f"{row.get('hpwl_records', 0)} | {final if final is not None else '待完成'} |")
    lines += ["", "## 轨迹比较", ""]
    for name, entry in result["pairwise"].items():
        divergence = entry["first_difference"]
        if divergence:
            lines += [f"- {name}：首个差异为第 {divergence['record'] + 1} 条记录。",
                      f"  - 基准：`{divergence['reference']}`", f"  - 对照：`{divergence['observed']}`"]
        else:
            lines.append(f"- {name}：已完成的共同前缀 {entry['common_prefix']} 条一致；未完成时不能推断全程一致。")
    lines += ["", "## 运行目录", ""]
    for row in result["runs"]:
        lines.append(f"- {row['label']}：`{row.get('run', '尚未创建')}`")
        if row.get("placement_sha256"):
            lines.append(f"  - 导出布局 SHA256：`{row['placement_sha256']}`；与历史旧版一致：{row['placement_matches_historical']}；"
                         f"与首次加速失败布局一致：{row['placement_matches_failed_fast']}。")
    if result["all_finished"]:
        lines += ["", "## 完成后的严格比较", ""]
        by_label = {row["label"]: row for row in result["runs"]}
        all_completed = all(row.get("status", {}).get("state") == "completed" for row in result["runs"])
        lines.append(f"- 四组 AMF 均正常完成：{all_completed}。本组没有执行 Vivado 后端或布线。")
        for a, b in [("a-legacy-legacy", "c-legacy-cached"),
                     ("b-assignment-legacy", "d-assignment-cached")]:
            left, right = by_label[a], by_label[b]
            if left.get("placement_sha256") and right.get("placement_sha256"):
                same = left["placement_sha256"] == right["placement_sha256"]
                lines.append(f"- {a} 与 {b} 的最终导出布局哈希一致：{same}。")
        a, b, c, d = [by_label[k] for k in ("a-legacy-legacy", "b-assignment-legacy",
                                           "c-legacy-cached", "d-assignment-cached")]
        if (all_completed and all(row["same_binary"] and row["same_controlled_inputs_and_parameters"]
                                  for row in result["runs"])
                and a.get("placement_matches_historical") and d.get("placement_matches_failed_fast")
                and a.get("placement_sha256") == c.get("placement_sha256")
                and b.get("placement_sha256") == d.get("placement_sha256")):
            lines.append("- 本次四组最终结果定位到求解器替换：旧求解器复现历史布局，新求解器复现首次加速布局；"
                         "两种候选排序在各自求解器下均未改变最终布局。此结论限定于本次 GETRF 配置。")
    (evidence / "comparison.md").write_text("\n".join(lines) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--evidence", default="experiments/evidence/20260929-placement-ablation")
    args = parser.parse_args()
    analyze(args.root, args.root / args.evidence)
