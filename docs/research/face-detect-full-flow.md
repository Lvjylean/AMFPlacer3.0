# faceDetect 完整 AMFPlacer 流程运行记录

启动日期：2026-09-25。服务器：`ssh eda072`；用户：`jinyang`。

本次范围是 **DCP → 重新导出 AMF 输入 → AMFPlacer 2.0 布局/打包 → Vivado 布局导入与布线 → routed DCP 和报告**。使用已存在的 routed DCP 作为网表及固定宏信息来源，在后端清空并重新导入布局。本次不包含从 RTL 重新综合、比特流生成或上板验证。

## 运行位置

```text
/Projects/jinyang/workspace/amf-runs/faceDetect-20260925-131037
```

首轮流水线 PID：`951100`，已于 2026-09-25 13:25:08 失败退出。所有阶段由同一个 runner 依次调度；任何阶段失败都会记录错误并停止后续执行。

实时状态以服务器文件为准：

```bash
ssh eda072 'cat /Projects/jinyang/workspace/amf-runs/faceDetect-20260925-131037/status.json'
```

阶段日志：

```text
logs/01_export.log
logs/02_amf.log
logs/03_vivado_route.log
logs/runner.log
```

## 输入与版本

- Vivado：`/Projects/Xilinx/Vivado/2024.2/bin/vivado`。
- 器件：`xcvu095-ffva2104-2-e`。
- DCP 来源：`/Projects/haoning/Vivado_Results/faceDetect_project/VCU108_PCIE_faceDetection/VCU108_PCIE_faceDetection.runs/impl_1/design_1_wrapper_routed.dcp`，已复制到本次 `inputs/reference.dcp`。
- AMFPlacer：复制 `/Projects/jinyang/AMF-Placer/build/AMFPlacer` 和相邻 `partitionHyperGraph` 到本次 `bin/`；使用已有二进制，没有重新编译。
- 当前源码提交、未提交补丁、配置、未跟踪 MLTimingModel 文件和输入哈希保存在 `manifest.json` 与 `provenance/`。已有二进制与当前工作区源码的构建对应关系尚未重新核验。
- 器件资源数据库和聚类数据从原 faceDetect 配置复制为独立快照。网表、时钟、固定位置、不可预测宏从选定 DCP 重新导出。
- 在启动 AMF 前检查旧聚类引用的每个单元名是否存在于新导出网表，避免把不匹配输入传入布局器。
- AMF 使用 8 个线程；Vivado 最大线程数设为 4。

## 运行适配

现有 AMF 导出代码把 `$errorNum` 单独输出为 Tcl 命令，Vivado 会把数字当作命令名。流水线保留 AMF 原始输出，仅在送入 Vivado 的副本中将这一行改为 `puts` 输出错误批次数；不改变布局命令或 AMF 算法。具体变更数与原始/适配文件哈希保存在 `reports/tcl_adapter.json`。

布局导入仍使用原有 `place_design -unplace`、`place_cell`、`place_design`、`route_design` 流程。没有额外加入历史部分实验使用的 `phys_opt_design`。

## 预期产物与结果口径

- `reports/input_validation.json`：输入数量、聚类引用与输入哈希。
- `placement/DumpCLBPacking-first-0.tcl`：AMF 原始布局脚本。
- `placement/placement_for_vivado.tcl`：本次运行的适配脚本。
- `reports/faceDetect_amf_routed.dcp`：后端成功后生成的 DCP。
- `reports/route_status.rpt`、`drc.rpt`、`timing_summary.rpt`、`utilization.rpt`、`bus_skew.rpt`。
- `manifest.json`：各阶段启动参数、运行时间和退出码。

`status.json` 为 `completed` 表示上述流程结束并已检查路由错误为 0；仍需审阅 DRC、时序、bus skew 和布局导入诊断，不能仅根据完成状态声称设计满足全部板级要求。

本地可复用启动脚本：[run_face_detect_flow.py](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/AMFplacer3.0/scripts/run_face_detect_flow.py)。该脚本应在 eda072 上运行，默认每次创建新的实验目录。不要为了查看状态再次启动它。

## 启动时的状态

2026-09-25 13:10:37 已启动。首次检查确认 Vivado 成功打开 DCP，并开始写入新的网表导出文件。此记录不是最终完成报告；后续结果以服务器上的实时状态和日志为准。

## 2026-09-25 13:40 状态核查：首轮失败

- 许可证已在 eda072 上验证生效，本次失败发生在 AMF 宏加载阶段。
- `01_export`：13:10:37 至 13:24:59，耗时 861.240 秒，退出码 0。导出 134,450 个单元，与历史输入的单元名集合一致；历史聚类引用 11,064 个单元，无缺失引用。
- `02_amf`：13:25:01 至 13:25:08，耗时 7.283 秒，退出码 -6（SIGABRT）。失败位置为 `InitialPacker::loadOtherCLBMacros`，断言 `cellInMacros.find(tmpCell) == cellInMacros.end()`，表明待加入 CLB 宏的单元已经属于某个宏。
- `03_vivado_route` 未启动。本次尚未生成 AMF 最终布局、重新布线后的 DCP、WNS/TNS 或最终 DRC 结果。原 runner 已不在运行，没有自动重试。

对比宏输入发现：历史文件仅有 54 个 LUTRAM 单元；本次文件有 1,179 条记录、797 个不同单元，其中 382 个单元重复出现，新增了 FDRE/FDCE/FDPE 寄存器。导出脚本会同时选取 RAM、ASYNC_REG、XPM_CDC 和 KEEP 单元。输入名称校验通过不足以证明宏约束兼容。

这将排查范围缩小到导出宏与 AMF 内部宏的重叠。重复行本身尚不能认定为直接根因，因为加载器会在每个站点内用集合去重；还需定位具体冲突单元及其已有宏。下一步应修正宏约束的生成或合并规则，保留同步器等设计约束，再重跑 AMF 和 Vivado 后端；不应直接删除新增约束来绕过断言。

## 后续根因确认

已用原二进制和原 DCP 确认：52 个 `KEEP=yes` 的 FDRE 已属于 CARRY 宏，又被外部宏文件按原 SLICE 分组，产生交叉归属；这些冲突单元没有 ASYNC_REG 或 XPM_CDC 属性。仅去重仍失败；在独立诊断输入中仅排除这 52 个单元后，CLB 宏加载通过。对照运行在进入全局布局前主动结束，完整流水线尚未重启。详见[宏兼容性根因](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/AMFplacer3.0/docs/research/face-detect-macro-compatibility.md)。
