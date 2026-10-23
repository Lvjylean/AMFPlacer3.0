#!/usr/bin/env python3
"""Read the current GETRF forward-bias pair; optionally finish its report."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import time

from analyze_placement_ablation import compare, trace


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")
    temporary.replace(path)


def analyze(root, evidence):
    manifest = json.loads((evidence / "manifest.json").read_text())
    result = dict(checked=dt.datetime.now().astimezone().isoformat(),
                  binary=manifest["binary"], binary_sha256=manifest["binary_sha256"],
                  scope="AMF only; concurrent process wall time is not pure placement time",
                  runs=[], references={})
    refs = {}
    for label, run_id in manifest["references"].items():
        run = root / "experiments/runs" / run_id
        lines, refs[label] = trace(run / "logs/amf.log")
        finals = re.findall(r"Current Total HPWL = ([0-9.]+)", "\n".join(lines))
        result["references"][label] = dict(run=str(run), final_hpwl=float(finals[-1]),
            placement_sha256=sha(run / "placement/DumpCLBPacking-first-0.tcl"))
    controls = None
    sequences = {}
    for item in manifest["runs"]:
        row = dict(label=item["label"], forward_bias=item["forward_bias"])
        result["runs"].append(row)
        launch = Path(item["launcher_log"])
        if not launch.exists() or not launch.read_text().splitlines():
            row["state"] = "launching"
            continue
        run = Path(launch.read_text().splitlines()[0])
        row["run"] = str(run)
        if not (run / "manifest.json").exists():
            row["state"] = "launching_or_failed"
            row["launcher_tail"] = launch.read_text()[-2000:]
            continue
        mf = json.loads((run / "manifest.json").read_text())
        status = json.loads((run / "status.json").read_text())
        row["state"] = status["state"]
        row["status"] = status
        row["stages"] = mf["stages"]
        row["same_binary"] = mf["binary_sha256"] == manifest["binary_sha256"]
        cfg = json.loads((run / "config.json").read_text())
        excluded = {"BipartiteMatchingForwardBias", "dumpDirectory", "BoundaryReportDirectory"}
        controlled = dict(config={k: v for k, v in cfg.items() if k not in excluded},
                          input_hashes={k: v["sha256"] for k, v in mf["inputs"].items()},
                          dcp_sha256=mf["input_dcp_sha256"], profiling=mf["runtime_profiling"])
        if controls is None:
            controls = controlled
        row["same_controlled_inputs_and_parameters"] = controlled == controls
        log = run / "logs/amf.log"
        if not log.exists():
            continue
        lines, sequence = trace(log)
        sequences[item["label"]] = sequence
        row["hpwl_records"] = len(sequence)
        row["latest_hpwl_record"] = sequence[-1] if sequence else None
        row["reference_comparisons"] = {key: compare(value, sequence) for key, value in refs.items()}
        row["latest_status"] = next((x for x in reversed(lines) if "STATUS:" in x), "")
        matchers = [x for x in lines if "AMF_MATCHER backend=" in x]
        row["matcher_calls"] = len(matchers)
        row["matcher_seconds"] = sum(float(re.search(r" solve_s=([0-9.eE+-]+)", x)[1]) for x in matchers)
        observed = sorted(set(float(re.search(r" forward_bias=([0-9.eE+-]+)", x)[1]) for x in matchers))
        row["observed_forward_bias"] = observed
        row["requested_bias_observed"] = observed == [float(item["forward_bias"])]
        if row["state"] == "completed":
            finals = re.findall(r"Current Total HPWL = ([0-9.]+)", "\n".join(lines))
            row["final_hpwl"] = float(finals[-1]) if finals else None
            placement = run / "placement/DumpCLBPacking-first-0.tcl"
            row["placement_sha256"] = sha(placement) if placement.exists() else None
            row["placement_matches_reference"] = {k: row["placement_sha256"] == v["placement_sha256"]
                                                   for k, v in result["references"].items()}
            if row["final_hpwl"] is not None:
                row["hpwl_change_percent_vs_reference"] = {k: 100 * (row["final_hpwl"] / v["final_hpwl"] - 1)
                                                           for k, v in result["references"].items()}
    if len(sequences) == 2:
        result["pairwise"] = compare(sequences["bias-0"], sequences["bias-001"])
    result["all_finished"] = all(r["state"] in {"completed", "failed", "stopped", "cancelled"}
                                 for r in result["runs"])
    if all(r.get("final_hpwl") is not None for r in result["runs"]):
        a, b = result["runs"]
        result["bias_hpwl_change_percent"] = 100 * (b["final_hpwl"] / a["final_hpwl"] - 1)
    save(evidence / "comparison.json", result)
    md = ["# GETRF 新求解器正向扰动对照", "", "更新时间：" + result["checked"], "",
          "两组使用同一构建、10 ns 目标和原始物理坐标配置，仅正向扰动参数不同。仅运行 AMF；未执行 Vivado placement/routing。",
          "", "| 组 | 状态 | 最终 HPWL | 匹配累计秒数 |", "|---|---|---:|---:|"]
    for row in result["runs"]:
        md.append(f"| {row['label']} | {row['state']} | {row.get('final_hpwl', '待完成')} | {row.get('matcher_seconds', 0):.3f} |")
    md += ["", "AMF 进程墙钟包含输入读取和输出适配，不称为纯布局时间。并发运行不用于隔离负载的速度比较。", "",
           "## 运行目录", ""]
    md += [f"- {r['label']}：`{r.get('run', '待创建')}`" for r in result["runs"]]
    if "bias_hpwl_change_percent" in result:
        md += ["", f"0.01 相对 0 的最终 HPWL 变化：{result['bias_hpwl_change_percent']:+.4f}%。"]
    (evidence / "comparison.md").write_text("\n".join(md) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--evidence", default="experiments/evidence/20260929-forward-bias")
    parser.add_argument("--watch", action="store_true", help="Finish this pair's report, with a four-hour limit")
    args = parser.parse_args()
    started = time.monotonic()
    while True:
        result = analyze(args.root, args.root / args.evidence)
        if not args.watch or result["all_finished"] or time.monotonic() - started > 4 * 3600:
            print(json.dumps(result, indent=2, ensure_ascii=False), flush=True)
            break
        time.sleep(60)
