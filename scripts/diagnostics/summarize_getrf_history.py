#!/usr/bin/env python3
"""Render the audited GETRF server inventory into Markdown and CSV (no DCP I/O)."""
import argparse
import csv
import json
import re
from datetime import datetime
from pathlib import Path


ROUTED = [
    ("20260926-173107-822886", "早期导出不完整诊断"),
    ("20260926-183758-400040", "首次完整 U250 基线"),
    ("20260927-063459-912642", "control：旧 X 修正＋SLR 惩罚"),
    ("20260927-063459-913333", "delay：物理边界延迟"),
    ("20260927-063459-912759", "cluster：边界延迟＋二维聚拢"),
    ("20260927-125004-730657", "严格导入修复后 delay"),
    ("20260928-014049-967020", "仅 SA 比例 0.71"),
    ("20260928-160231-470820", "共享及 SA 比例均为 0.71"),
]
NOTES = {
    "20260926-150155-081308": "无固定 I/O 种子，聚类初始化不退出，主动停止。",
    "20260926-150648-975751": "SRL 资源分类/密度网格断言失败。",
    "20260926-151909-521419": "完成布局打包，但 DumpCLBPacking 路径重复拼接导致导出失败。",
    "20260926-161904-089708": "DirectMacroLegalize 导致大幅反复位移，主动停止。",
    "20260926-163444-325508": "分数列预算取整为 0，合法化无进展，主动停止。",
    "20260926-170252-047980": "减少前期迭代至 9 后末段 QP 发散，出现 NaN，退出 2。",
    "20260926-173107-822886": "缺少 298 个 MUXF7、91 个 MUXF8 的 BEL 导出；后端虽布通，AMF 完整性验收未通过。",
    "20260926-183758-400040": "完整导出并布通；仍由 Vivado 修复少量未接受位置，setup 未收敛。",
    "20260927-032031-594482": "时序权重过大导致 QP 坐标 NaN，control 失败；另两组随之停止统一修复。",
    "20260927-041854-078213": "AMF 已完成；为统一构建重跑三组，主动停止旧后端并释放资源。",
    "20260927-041854-079412": "3774 个 PU 打包停滞；定位遗留硬时钟列筛选后主动停止。",
    "20260927-041854-083355": "AMF 已完成；为统一构建重跑三组，主动停止旧后端并释放资源。",
    "20260927-125907-199280": "control 导入验证的 AMF 阶段失败，256 个 PU 因时钟列搜索限制未完成打包。",
    "20260927-125913-760094": "cluster 完成 AMF 与严格导入，856998 个原 LOC/BEL 全匹配；未运行 placement/routing。",
    "20260927-144536-634632": "修复跨列搜索回退后，control 完成 AMF 与严格导入；未运行 placement/routing。",
    "20260926-151115-924561": "仅初始打包：198 个 SRL/MUX 宏、594 个真实单元；非完整 placement。",
    "20260927-124630-816660": "仅初始打包：CLB 修复后的完整输入检查；非完整 placement。",
    "20260926-143230-527023": "硬资源导入后，审计未处理 URAM BEL 的类型前缀而失败；不是资源放置错误的证据。",
    "20260926-143933-202523": "硬资源验证通过，26096 个单元 LOC/SLR 匹配；仅部分资源 DCP。",
    "20260926-213914-179421": "复用已布局 DCP，尝试 AlternateCLBRouting；默认流程成功后主动停止，未完成路由。",
    "20260926-151231-308935": "Vivado 局部 SRL/MUX 验证：594/594 LOC/BEL 全匹配；未写出最终 DCP。",
    "20260928-005532": "SA 距离比例标定/归档记录，0.32→0.71；不是全流程，归档时间不代表采样总耗时。",
}


def note(run):
    rid = run["id"]
    for suffix, text in NOTES.items():
        if rid.endswith(suffix):
            return text
    if "20260927-030230" in rid:
        return "RAMB36 与虚拟 RAMB18 占位重复计数，三组主动停止并统一修正。"
    if "20260927-032031" in rid:
        return "control 出现 NaN 后，停止本组，以相同数值保护重新运行。"
    if "20260927-041344" in rid:
        return "首轮 QP 装配性能问题，主动停止改为一次性稀疏装配；缺少结束时间。"
    if run["summary"].get("timing"):
        return "已完成完整布局布线；时序、合法性和导入验收分别记录。"
    return run["status"].get("reason", "")


def mode(run):
    c = run["config"]
    if c.get("BoundaryAwareClustering") == "true":
        return "cluster"
    if c.get("PhysicalBoundaryMode") == "true":
        return "delay"
    if c.get("PhysicalBoundaryMode") == "false":
        return "control"
    return "早期适配/专项验证"


def elapsed(start, finish):
    return ((datetime.fromisoformat(finish) - datetime.fromisoformat(start)).total_seconds()
            if start and finish else None)


def minute(value):
    return "—" if value is None else f"{value / 60:.2f}"


def ns(value, signed=False):
    if value is None:
        return "—"
    if value == 0:
        return "0"
    return (f"{value:+.3f}" if signed else f"{value:.3f}").replace("-", "−")


def record(run, names):
    m, s, z, c = (run[k] for k in ("manifest", "status", "summary", "config"))
    stages = {x["name"]: x for x in run["stages"]}
    v = {}
    for line in run.get("stages_tsv", "").splitlines()[1:]:
        k, value = line.split("\t")
        v[k] = float(value)
    for k, value in z.get("vivado_stages_seconds", {}).items():
        assert abs(value - v[k]) < 0.00001, (run["id"], k)
    t = z.get("timing", {})
    raw = run.get("raw_timing_values")
    if t:
        assert raw is not None, run["id"]
        assert raw[:3] == [t["wns_ns"], t["tns_ns"], t["setup_failing_endpoints"]]
        assert any(line.split()[:2] == ["ap_clk", "10.000"] for line in run["clock_lines"])
        assert m["input_dcp_sha256"] == "6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031"
        assert float(m.get("amf_clock_period_ns", c["ClockPeriod"])) == 10
    total = elapsed(m.get("started"), s.get("finished"))
    amf = stages.get("amf", {}).get("elapsed_seconds")
    vivado = stages.get("vivado", stages.get("vivado_route_retry", {})).get("elapsed_seconds")
    if vivado is None and ("resources-" in run["id"] or "srl-audit-" in run["id"]):
        vivado = s.get("elapsed_seconds")
    final = next((d for d in run["dcps"] if d["path"].endswith("/getrf_routed.dcp")), None)
    label, title = names.get(run["id"], ("", mode(run)))
    category = ("routed" if final else "import-only" if s.get("strict_import_verified")
                else "incomplete" if "-full-" in run["id"] else "auxiliary")
    other = (total - amf - v["place"] - v["route"]) if final else None
    if final:
        assert total is not None and other >= 0
    r = dict(label=label, title=title, run_id=run["id"], category=category, mode=mode(run),
             state=s.get("state"), started=m.get("started"), finished=s.get("finished"),
             wns_ns=t.get("wns_ns"), tns_ns=t.get("tns_ns"),
             setup_failing_endpoints=t.get("setup_failing_endpoints"), whs_ns=t.get("whs_ns"),
             hold_failing_endpoints=t.get("hold_failing_endpoints"),
             amf_seconds=amf, vivado_seconds=vivado, open_seconds=v.get("open"),
             import_seconds=v.get("import"), place_seconds=v.get("place"), route_seconds=v.get("route"),
             wall_seconds=total, other_seconds=other,
             strict_import_verified=s.get("strict_import_verified", z.get("strict_import_verified")),
             amf_export_complete=z.get("amf_export_complete"),
             implementation_verified=z.get("implementation_verified"), timing_met=z.get("timing_met"),
             loc_bel_retention=z.get("routed_placement", {}).get("exact_retention_ratio"),
             shared_y2x_ratio=c.get("y2xRatio"), sa_ratio_override=c.get("Simulated Annealing y2xRatio"),
             sa_ratio_effective=(float(c["Simulated Annealing y2xRatio"]) if "Simulated Annealing y2xRatio" in c
                                 else 0.8 * float(c["y2xRatio"]) if "y2xRatio" in c else None),
             amf_clock_period_ns=m.get("amf_clock_period_ns", c.get("ClockPeriod")),
             vivado_ap_clk_ns=10 if run.get("clock_lines") else None,
             amf_jobs=c.get("jobs"), vivado_version=run.get("tools", {}).get("vivado_version"),
             vivado_threads=run.get("tools", {}).get("max_threads"),
             source_commit=m.get("source_commit"), binary=m.get("binary"), binary_sha256=m.get("binary_sha256"),
             input_dcp_sha256=m.get("input_dcp_sha256"),
             final_dcp=final["path"] if final else None,
             final_dcp_bytes=final["bytes"] if final else None,
             final_dcp_recorded_sha256=s.get("output_dcp_sha256") if final else None,
             placed_dcp=next((d["path"] for d in run["dcps"] if d["path"].endswith("/getrf_placed.dcp")), None),
             partial_dcp=next((d["path"] for d in run["dcps"] if d["path"].endswith("/getrf_hard_resources_partial.dcp")), None),
             stage_exit_codes="; ".join(f"{k}={v.get('exit_code')}" for k, v in stages.items()),
             notes=note(run), server_run_path=run["run_path"])
    return r


def table(rows):
    lines = ["| 指标 | " + " | ".join(r["label"] + " " + r["title"] for r in rows) + " |",
             "|---|" + "---:|" * len(rows)]
    metrics = [
        ("开始时间（UTC+8）", lambda r: r["started"][5:16].replace("T", " ")),
        ("WNS（ns）", lambda r: ns(r["wns_ns"], True)),
        ("TNS（ns）", lambda r: ns(r["tns_ns"])),
        ("setup 违例端点", lambda r: str(r["setup_failing_endpoints"])),
        ("AMF 耗时（分钟）", lambda r: minute(r["amf_seconds"])),
        ("Vivado placement（分钟）", lambda r: minute(r["place_seconds"])),
        ("Vivado routing（分钟）", lambda r: minute(r["route_seconds"])),
        ("总墙钟（分钟）", lambda r: minute(r["wall_seconds"])),
        ("其中：AMF 布局导入（分钟）", lambda r: minute(r["import_seconds"])),
        ("AMF/placement/routing 之外合计（分钟，含导入）", lambda r: minute(r["other_seconds"])),
        ("WHS（ns）", lambda r: ns(r["whs_ns"], True)),
        ("hold 违例端点", lambda r: str(r["hold_failing_endpoints"])),
        ("共享 / SA 有效比例", lambda r: f"{r['shared_y2x_ratio']} / {r['sa_ratio_effective']:.2f}"),
        ("原 LOC/BEL 保留率", lambda r: "—" if r["loc_bel_retention"] is None else f"{100*r['loc_bel_retention']:.4f}%"),
        ("严格导入验收", lambda r: "通过" if r["strict_import_verified"] else "旧流程，允许后端修复"),
        ("AMF 完整性＋实现验收", lambda r: "通过" if r["implementation_verified"] else "未通过"),
    ]
    for label, fn in metrics:
        lines.append("| " + label + " | " + " | ".join(fn(r) for r in rows) + " |")
    lines += ["", "本表最终 DCP（均在 eda072）：", ""]
    for r in rows:
        lines += [f"- **{r['label']} {r['title']}**：`{r['final_dcp']}`"]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.inventory.read_text())
    names = {"getrf-u250-full-" + suffix: (f"R{i:02}", title) for i, (suffix, title) in enumerate(ROUTED, 1)}
    rows = [record(r, names) for r in data["runs"]]
    names_by_id = {r["run_id"]: r for r in rows}
    routed = [names_by_id[k] for k in names if k in names_by_id]
    unlabelled = [r for r in rows if r["final_dcp"] and not r["label"]]
    for i, r in enumerate(unlabelled, len(routed) + 1):
        r["label"] = f"R{i:02}"
    routed += unlabelled
    def sort_key(r):
        if r["started"]:
            return r["started"]
        match = re.search(r"(20\d{6})-(\d{6})", r["run_id"])
        # Directory timestamp is only a sorting aid, never a measured start time.
        return datetime.strptime("".join(match.groups()), "%Y%m%d%H%M%S").isoformat() if match else r["run_id"]
    rows.sort(key=sort_key)
    intro = f"""# GETRF / U250 历次实验记录

数据核对时间：{data['collected_at']}。服务器：`eda072`（jinyang）；根目录：`{data['server_root']}`。

共 {len(rows)} 条正式运行目录记录，其中 {len(routed)} 轮生成最终 routed DCP。R01 的 AMF 导出不完整，只作诊断；其余 7 轮通过完整导出与实现验收，其中 3 轮满足 setup 时序。另有严格导入验证、失败/主动停止和辅助标定，逐条列于后文。范围为 `experiments/runs/` 内 GETRF 相关记录；单元测试、微小器件预检和不生成新布局的报告分析不另算完整实验。

## 口径与可比性

- 8 轮最终结果均使用原始 GETRF post-opt DCP，SHA256 `6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`；器件 `xcu250-figd2104-2L-e`，Vivado 2024.2。已逐轮核对 AMF 目标 10 ns 及 Vivado `ap_clk=10.000 ns`。这是 OOC 内核实验，不是完整板级实现。
- AMF 耗时来自 `manifest.json` 的 AMF 进程 elapsed_seconds，包含 AMF 内部处理、布局、打包、导出与退出；placement/routing 分别取 Vivado `place_design`/`route_design` 的完整调用耗时，routing 包含其内部迭代和重布线。
- 总墙钟统一为 `status.finished − manifest.started`。含 AMF、布局导入、Vivado 布局布线、审计、报告、DCP 写出、部分轮次的边界时序抽样以及外围处理；**不含初始综合/post-opt、首次 DCP→AMF 网表导出、源码构建和开发时间**。本批均复用导出缓存。
- “其他合计”＝总墙钟−AMF−placement−routing，已包含导入，不能再次加上导入。它还含审计/报告等实验开销，不等同于工具适配的纯必要成本。各行独立四舍五入可能有 0.01 分钟差异。旧三组比较文档从更外层启动器计时，比本表多约 0.46 秒；本表统一采用运行目录自身时间戳。
- `—` 表示未执行、未完成或没有可靠记录，不能当作 0。停止时尚未结束的阶段不估算完整耗时。阶段失败的 AMF/工具时间是直到失败/停止的已记录时间。
- 历史实验存在并行和共享服务器负载；不能将跨构建、跨轮次差异都归因于单个参数。R03–R05 同一冻结二进制、三组并行；R07–R08 同一二进制，SA 均 0.71，共享比例由 0.4 改为 0.71。历史配置以每轮快照为准，不能用当前配置反推。
- 本次只核验 DCP 路径及文件大小；CSV 中 SHA256 为当时 status 记录，未重新读取全部 DCP 计算哈希。DCP 只保存在服务器。

## 早期 U250 适配（09-26）

R01 漏导出 389 个 MUX 的 BEL，后端完成布线不能替代 AMF 完整性验收。R02 是首次完整 U250 基线，尚未包含后来加入的 SLR 边界延迟。

{table(routed[:2])}

## 物理边界三组对照（09-27，严格导入修复前）

R03 control 使用旧 X 方向经验修正＋SLR 1.5 ns；R04 delay 使用实际物理边界模型；R05 cluster 在 R04 上增加容量感知二维聚拢。三组共同使用时序权重上限与 QP 数值保护。三组当时均允许 Vivado 修复导入拒绝的位置，不能与严格导入版混为同一阶段。

{table(routed[2:5])}

## 严格导入修复与横纵比例实验（09-27 至 09-28）

R06–R08 均为 delay 模式，二维聚拢关闭，严格导入及最终原 LOC/BEL 保留率均为 100%。R06 和 R07 还存在既有 CLB 打包稳健性代码差异；R07→R08 保持同一二进制与 SA=0.71，改动共享 y2xRatio。R06 的 WNS 余量仍优于 R08；R08 是本次历史记录中总墙钟最短的完整通过轮次。

{table(routed[5:8])}
"""
    if len(routed) > 8:
        intro += "\n## 后续新增完整轮次\n\n" + table(routed[8:]) + "\n"
    lines = [intro, "\n## 其余运行逐条记录\n",
             "以下均没有最终 routed DCP，因此不填写最终 WNS/TNS。记录为 completed 的 import-only/packing/标定轮次仅代表其限定任务完成。原始状态为 failed 但备注写主动停止的，按历史退出状态保留，不改写成算法失败。时间单位为分钟。\n",
             "| 运行 ID | 模式/范围 | 原始状态 | AMF | Vivado 进程 | 布局导入 | 总墙钟 | 结果/原因 |",
             "|---|---|---|---:|---:|---:|---:|---|"]
    for r in rows:
        if r["final_dcp"]:
            continue
        lines.append(f"| `{r['run_id']}` | {r['mode']} / {r['category']} | {r['state']} | {minute(r['amf_seconds'])} | {minute(r['vivado_seconds'])} | {minute(r['import_seconds'])} | {minute(r['wall_seconds'])} | {r['notes']} |")
    lines += ["", "上表中的 category：import-only＝仅严格导入；incomplete＝计划流程未完成；auxiliary＝硬资源、初始打包、局部验证、备用路由或标定。packing-only 缺少结束时间，保留 AMF 进程计时，不将其冒充总墙钟。", "", "上表仅有以下部分资源 DCP，**不代表完整布局布线**：", ""]
    for r in rows:
        if r["partial_dcp"]:
            lines.append(f"- `{r['partial_dcp']}`")
    lines += ["", "## 构建和原始证据", "", "下面的启动提交是 manifest 中的 source_commit；实际运行可能包含未提交修改，应结合冻结二进制和 inputs/build_manifest.json、working_tree.patch 追溯。CSV 还保存完整二进制哈希、输入哈希、最终 DCP 记录哈希及退出状态。", "", "| 轮次 | 运行 ID | 启动提交 | 冻结构建目录 |", "|---|---|---|---|"]
    for r in routed:
        lines.append(f"| {r['label']} | `{r['run_id']}` | `{r['source_commit'][:8]}` | `{r['binary'].split('/builds/', 1)[-1]}` |")
    lines += ["", "每轮原始证据位于服务器对应运行目录：`manifest.json`、`status.json`、`config.json`、`reports/summary.json`（若完成）、`reports/stages.tsv`、`reports/timing_summary.rpt`。本表的 8 轮时序已与原始 timing_summary 的 WNS/TNS/端点数交叉核验，阶段计时已与 stages.tsv 交叉核验。", "", "每轮还保留以下 **placement 中间 DCP**，不作为本表最终时序结果：", ""]
    lines += [f"- {r['label']}：`{r['placed_dcp']}`" for r in routed if r["placed_dcp"]]
    lines += ["", "相关历史说明：[全流程适配](u250-getrf-full-flow.md)、[物理边界三组验收](device-physical-boundary-getrf-validation.md)、[严格导入修复](clb-import-legality.md)、[SA 0.71 初次 10 ns 实验](u250-sa-ratio-10ns-validation.md)。", "", "配套 CSV：[逐轮原始数值与路径](getrf-u250-experiment-history.csv)。库存快照和验证哈希在服务器 `experiments/evidence/20260928-getrf-experiment-history/inventory.json`；本地轻量副本在 `local-reports/getrf-u250-history-20260928/inventory.json`。", ""]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    prefix = args.output_dir / "getrf-u250-experiment-history"
    prefix.with_suffix(".md").write_text("\n".join(lines))
    with prefix.with_suffix(".csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(json.dumps({"records": len(rows), "routed_dcps": len(routed), "validated_timing_reports": len(routed),
                      "outputs": [str(prefix.with_suffix(x)) for x in (".md", ".csv")]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
