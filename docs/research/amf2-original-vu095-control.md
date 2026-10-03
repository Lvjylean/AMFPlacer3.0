# 原版 AMF-Placer 2.0 的 VU095 对照实验

2026-09-30，用户授权对 MiniMap2、OptimSoC、MemN2N 运行原版 AMF-Placer 2.0，采用作者发布的周期与原始 VU095 算例。

## 保留的原版代码

- 2026-09-30 核对官方 README 后补充：这里的“原版”特指官方公开的 AMF-Placer 2.0 基础实现，不等于论文使用的进阶版。README 说明公开版的关键路径延迟和运行时间平均约有 5% 的性能差距；复现论文结果的进阶版需要向作者申请。不能用这个平均数直接解释某个算例的 WNS 差距。
- 旧仓库：服务器 `/Projects/jinyang/AMF-Placer`。其工作目录存在修改，不能把目录内现成二进制直接视为纯原版。
- 本轮从该仓库的官方 Git 对象导出提交 `70d98288153046ea4fd07190e748b6530e3042f5`；已经核对这是官方 GitHub 当前 HEAD。未包含工作区修改。
- 独立构建：`/Projects/jinyang/workspace/AMFplacer3.0/builds/upstream-amf2-20260930-101827-215260`，原始源码在 `src/`，二进制在 `build/AMFPlacer`。构建前后源码哈希一致。
- AMFPlacer SHA-256：`f92a3065e43e92a3fb557201910d0c6bdc2f76f74a46af4a2655accc81577e21`。
- 旧 `Timing-driven` 标签提交 `179f9658b15c607c7e21535794b7919eff7d9b69` 也在仓库中。当前选择的是官方维护后的 2.0 代码，不声称它就是论文实验时的确切提交。
- 没有修改现有 `builds/current`，没有混入 AMFplacer3.0 的 R10、多 SLR、时序权重保护或性能插桩代码。

## 输入与约束

| case | 原始 AMF 单元数 | 作者 AMF ClockPeriod | 后端输入 |
|---|---:|---:|---|
| MemN2N | 289,721 | 10 ns | 原包 `xillydemo_placed.dcp` |
| OptimSoC | 468,150 | 20 ns | 原包 `system_2x2_cccc_vcu108_routed.dcp` |
| MiniMap2 | 681,889 | 8 ns | 原包 `design_1_wrapper_placed.dcp` |

周期来自作者 `benchmarks/testConfig/` 配置；不能用论文报告的关键路径延迟代替目标周期。本轮已将三份配置和每个 AMF 输入文件与官方提交的 Git blob 逐文件核对 SHA-256。

只改配置中的输入绝对路径与实验输出目录。保留作者的 8 线程、30 轮全局布局、模拟退火参数及所有算法设置。目标器件为 `xcvu095-ffva2104-2-e`，使用完整设计及其 PCIe、DDR、I/O 等资源，不抽取 OOC 核，不重新综合，不应用 U250 MemN2N 初始化修复。

Vivado 保留原始 DCP 的完整多时钟、例外和 I/O 约束，不把所有时钟统一改成 AMF 的单一周期。部分派生时钟有取整差异，如 MemN2N 9.999 ns、OptimSoC 19.992 ns，仍保留原约束。时钟清单及哈希写入每个实验的输入来源记录。

原有 DCP 配对证据已确认全部单元名称和类型一致。本轮另检查了三个算例共 8,982,130 条原始 AMF 引脚记录：比较规范化叶单元引脚的方向和驱动集合，常量源按 GND/VCC 类型归一化，内部 Unisim 驱动按原包显式输出引脚映射归一化，双向 I/O 计入潜在驱动。三项均通过，未发现剩余连接差异。检查范围不包括顶层端口连线，也不把无下游叶引脚的悬空输出网络表示差异视为连接变化；这不是形式化功能等价证明。首次名称差异及双向 I/O 检查修订均保留在报告中。

## 执行和测量

批次：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/amf2-vu095-original-batch-20260930-101827`。

构建和运行通过 `scripts/amf3.py` 入口记录。AMF 阶段按规模串行运行 MemN2N、OptimSoC、MiniMap2。布局与后端使用各自独立的实验目录，后端复用已完成的 AMF 输出，避免重复计算。

原版后端模式保留作者生成的放置目标和“导入后调用 `place_design`、`route_design`”流程；仅修复导出 Tcl 中将 `$errorNum` 当命令执行以及错误日志字符串未转义的问题，拆分阶段以记录耗时。不会追加 3.0 的固定单元回填或 SRL BEL 修正。记录导入拒绝、重试、原始导出覆盖范围，以及 Vivado 完成后 AMF 位置的保留比例。原版没有导出的资源仍由原始 DCP 约束和 Vivado 处理，不能将这种覆盖范围称为 3.0 的严格全单元导入通过。

记录 AMF 单独耗时、适配耗时、Vivado 阶段耗时、工具退出码、WNS/WHS、布线完整性、DRC 和最终 DCP 哈希。只读连接审计可能与后续 AMF 阶段重叠，因此当前墙钟时间不能作为独占服务器的论文性能复现。最终 DCP 只保存在服务器，本地仅保存轻量记录。

## 与论文及 U250 实验的比较边界

- AMF-Placer 2.0 论文将 OptimSoC 排除，理由是其主要违例涉及跨时钟域。因此它是用户要求的补充实验，不是论文表格中的复现项。
- 论文后端使用 Vivado 2020.2/2021.2；本机已安装版本为 2024.2。本轮结果必须标注版本差异。
- 先前 U250 运行采用提取后的核心网表，MemN2N 还修复了源代码初始化输入；本轮则使用原包完整 VU095 网表。这两组可以检查各自流程是否工作，不能把差值单独归因于 AMF 2.0 与 3.0 的算法变化。

来源：[官方代码](https://github.com/zslwyuan/AMF-Placer)、[AMF-Placer 2.0 论文](https://arxiv.org/pdf/2210.08682v2)。

## 公开正 WNS 证据与本轮负值诊断

官方 `doc/pages/_3_2_ExperimentalResults_2.md` 和论文表 III 均报告 MiniMap2 的 AMF-V2020 / AMF-V2021 WNS 为 +0.049 / +0.001 ns；同表 MemN2N 为 −1.601 / −1.665 ns。benchmark 用于比较布局质量，并不保证在给定目标周期下全部收敛。

作者提供的三个原始 Vivado 工程压缩包另含顶层布线后报告。它们由 Vivado 2020.1 生成，不能标为 AMF-Placer 2.0 的输出：MemN2N WNS −1.364 ns，OptimSoC −5.127 ns，MiniMap2 +0.033 ns。MiniMap2 的核心时钟为 8 ns，TNS 为 0、WHS +0.029 ns、THS 为 0；报告仍存在 1 项 no_input_delay 和 3 项 no_output_delay，不能据此声称所有板级 I/O 时序均已验证。

原始报告、实现 Tcl、日志和压缩包来源/hash 清单保存在 `experiments/evidence/20260930-amf2-published-timing/`，本地同步的 12 个报告/日志/脚本均已逐文件核对 SHA-256；没有下载 DCP。

- 本轮 MemN2N 最差路径位于 `fc_w_sm/exp`，同一 9.999 ns 时钟域，18 级逻辑，数据路径 14.639 ns（逻辑 3.575 ns、布线 11.064 ns，占 75.579%），时钟偏斜 −0.090 ns，WNS −4.744 ns。布线延迟占主要部分，但尚未隔离导致本轮比论文更差的具体因素。
- 本轮 OptimSoC 最差路径从 DDR 校准完成寄存器 `calDone_gated_reg` 到 CPU ITLB，源时钟 `mmcm_clkout0` 为 3.332 ns、目标时钟 `clk` 为 19.992 ns，边沿关系产生的 setup requirement 为 3.332 ns。数据路径 11.709 ns，其中布线 10.851 ns（92.672%），WNS −7.523 ns。原包的最差路径也来自同一 DDR 校准信号并跨这两个时钟域。当前还存在同域 DDR 路径违例，不能假设处理最差跨域路径即可全部收敛。

## 状态

截至 2026-09-30 15:58（香港时间），官方公开基础版构建、三项连接核验、三项 AMF 布局和三个后端均完成。MiniMap2 于 15:56:55 完成重试。

| case | AMF 退出码 | AMF 流程墙钟时间 | Vivado 后端 |
|---|---:|---:|---|
| MemN2N | 0 | 284.5 s | 11:02 完成布局布线 |
| OptimSoC | 0 | 415.6 s | 11:31 完成布局布线 |
| MiniMap2 | 0 | 605.1 s | 首次检查失败；15:21 重启后端，15:56 完成布局布线 |

| case | WNS (ns) | WHS (ns) | 布线完成率 | DRC Error / Critical Warning | 请求 LOC/BEL 最终保留率 |
|---|---:|---:|---:|---:|---:|
| MemN2N | −4.744 | +0.030 | 100% | 0 / 0 | 99.46% |
| OptimSoC | −7.523 | +0.030 | 100% | 0 / 0 | 99.74% |
| MiniMap2 | −0.329 | +0.030 | 100% | 0 / 0 | 99.89% |

三个算例均有 setup 违例，hold 通过；最终 DCP 已保存在服务器。后端使用 Vivado 2024.2、4 线程。原始 Tcl 允许导入重试后由 Vivado 完成布局，这些结果不能称为严格保持全部 AMF 位置的实验。通用报告中的 `strict_import_verified`、`amf_export_complete` 仍为 false，保留原版导出覆盖范围与实际位置修复的证据。

MiniMap2 重试退出码为 0，后端总墙钟 2137.249 s，其中 AMF→Vivado 准备 21.881 s、Vivado 导入 646.336 s、`place_design` 423.934 s、`route_design` 785.412 s，其余为读 DCP、审计、报告和写出等阶段。全部 463,557 条可布线网络完成，routing errors 为 0，DRC errors/critical warnings 为 0（仍有普通 warnings 1,054 条）。TNS −4.176 ns、49 个 setup 端点违例，THS 0、hold 违例为 0。最差路径为原始 8 ns 时钟域内 14 级逻辑，数据路径 8.086 ns（逻辑 2.118 ns、布线 5.968 ns），时钟偏斜 −0.269 ns。最终 DCP SHA-256 为 `7334e5dbe9b6368def13217480b9da9d8f37bdfd61737dd4f086db2d4705dad1`；本地已同步 31 个轻量文件，没有下载 DCP。

MiniMap2 首次后端在 11:31 因检查器名称解析失败而停止，尚未进入 Vivado。27 个单元的原始名称含连续反斜杠，Tcl 列表读取使审计名称少了一层转义。独立 Vivado 探针确认 27 个名称均能找到并原位放置成功。修复只在审计清单中恢复同时出现在原始网表和导出命令中的字面名称，目标 LOC/BEL 必须完全相同；原始放置命令不改写。18 项后端测试通过，新实验 7,058 个放置批次的命令载荷逐一与原版一致。

重试实验：`amf2-original-minimap2-vu095-backend-retry1-full-20260930-152118-207915`，复用已完成的原版 AMF 布局，没有重跑 AMF，没有修改算法、8 ns 周期或输入 DCP。首次失败实验和批次 `backend_status.json` 原样保留；重试状态独立记录在 `backend_retries.json`。阶段汇总优先读取最新重试并保留历史尝试。

运行 `python3 scripts/diagnostics/summarize_amf2_original_vu095.py <批次绝对路径>` 可更新 `reports/results.json`。本地 `local-reports/` 已保存构建来源、配置、核验报告和已完成的时序/布线报告，没有下载 DCP。

MemN2N 导入记录：首次导入有 415 个失败批次，原版重试记录有 783 次拒绝事件，主要可见寄存器复位控制集冲突；这些数值不是不同失败单元数。289,412 个请求位置中，导入后 287,823 个精确匹配，288,177 个已放置；最终由 Vivado 补全放置，287,845 个请求 LOC/BEL 保持一致。没有为了消除这些记录而修改原版算法或启用 3.0 BEL 修正。
