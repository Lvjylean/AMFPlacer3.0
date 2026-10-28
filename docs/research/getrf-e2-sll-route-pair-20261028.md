# GETRF / U250：现有 E2 DCP 的 SLL 约束布线对照

两份 DCP 均完成独立完整布线。**无 SLL 布线约束版满足当前 10 ns 约束，带 SLL 约束版未满足 setup 时序。** 两组最终 cell 的 LOC/BEL 相同且未被路由改变；输入端点锁定属性存在差异，不能将时序差值全部归因于 SLL 位置。

当前状态：两次完整布线、正式报告、DCP 保存和补充全量路径审计均已完成。

## 结果

| 指标 | 无 SLL 布线约束 | 带 SLL 布线约束 |
| --- | ---: | ---: |
| Vivado 时钟周期 | 10 ns | 10 ns |
| route_design | 成功 | 成功 |
| 可布线 / 全部布通 net | 921,820 / 921,820 | 921,820 / 921,820 |
| 路由错误 | 0 | 0 |
| WNS | **+0.060 ns** | **−0.499 ns** |
| TNS | 0 ns | −7.606 ns |
| setup 失败端点 | 0 / 607,963 | 65 / 607,963 |
| WHS / THS | +0.020 / 0 ns | +0.020 / 0 ns |
| hold 失败端点 | 0 | 0 |
| 实际 SLL | **8,463** | **8,467** |
| 使用 SLL 的 net | 8,442 | 8,442 |
| route_design 墙钟 | 1,725.547 秒（28.759 分钟） | 1,626.873 秒（27.115 分钟） |
| 主验证进程墙钟 | 2,141.991 秒 | 2,051.913 秒 |
| primitive cell LOC/BEL 变化 | 0 | 0 |
| cell 增加 / 删除 | 0 / 0 | 0 / 0 |
| DRC Error / Critical Warning | 0 / 0 | 0 / 0 |

WNS 来自路由结束后的 `report_timing_summary -delay_type min_max -report_unconstrained`，不是路由中间估计。两份 DCP 均被 Vivado 识别为 **OOC 设计**：303 个端口属于 implicitly routed ports，部分顶层端口缺少 HD.PARTPIN_LOCS，ap_clk 缺少 HD.CLK_SRC；外部接口有未约束路径。因此“满足 10 ns”限定于这份核心 DCP 的现有用户时序约束，不代表完整板级设计或 bitstream 验收。

两组 DRC 都保留 DSP 输入/输出流水线警告及 RTSTAT-10 等提示，Vivado 明确说明 OOC 模式未执行全部 connectivity DRC。路由成功、时序达标和完整系统验收是不同检查项。

## 实验设置与可比性

每份 DCP 在新 Vivado 2026.1 进程中只调用一次默认 `route_design`，最大 4 线程；带约束版先运行，无约束版后运行。不重跑 AMF、不重跑 placement、不做增量导入，不单独调用 phys_opt。默认 route_design 内置的关键路径优化保留，日志中带约束版执行了该优化，但没有改变 cell 位置。

两份输入均来自同一 E2 布局。无约束组仍包含 E2 在布局阶段的 SLL 规划影响，只是没有应用 FIXED_ROUTE；它不是 E1 或普通 Pro。

| 输入属性 | 无约束版 | 带约束版 |
| --- | ---: | ---: |
| primitive cell | 934,775 | 934,775 |
| IS_LOC_FIXED | 879,526 | 879,526 |
| IS_BEL_FIXED | 878,879 | 879,391 |
| 非空 LUT LOCK_PINS | 0 | 76,516 |
| 目标 net 的非空 FIXED_ROUTE | 0 | 8,442 |

两份文件按原样布线，没有归一化这些锁定属性。带约束版额外固定 512 个 BEL，并固定 76,516 个 LUT 的输入映射。本次可以说明这两份实际产物的差异，不能单独量化 SLL 选点与 LUT 输入锁定各自造成多少延迟。

两次路由串行；只读路径诊断和补充审计与无约束路由部分重叠，服务器也未做独占调度。表中为实际测得耗时，不能据此精确归因 SLL 约束增加或减少了多少运行时间。主进程墙钟包含 DCP 加载、报告、保存及原始审计，不含另行启动的补充审计，不能当成 AMF 布局时间。

## SLL 数量与物理 COLUMN 分布

计数对象为已布线 net 上 `IS_CROSSING_SLRS && IS_SLL` 的唯一物理 node，全部为 SIGNAL；没有同一 SLL 被多个 net 对象占用。不是日志中的 Estimated SLL Demand。

| 边界 | 无约束版 | 带约束版 |
| --- | ---: | ---: |
| SLR0–1 | 7,864 | 7,868 |
| SLR1–2 | 599 | 599 |
| SLR2–3 | 0 | 0 |
| 总计 | 8,463 | 8,467 |

两组跨 die net 集合完全相同。无约束版有 8,421 条 net 各使用 1 根 SLL、21 条各使用 2 根；带约束版为 8,417 条各使用 1 根、25 条各使用 2 根。差异全部来自 `grp_kernel_getrf_0_Pipeline_LoopRead_VITIS_LOOP_165_1_fu_1127/matA_{1,2,3,4}_we0_INST_0_i_1_n_3`：固定方案各要求 2 根，Vivado 自由选择时各使用 1 根。

8,442 条 net 中，5,864 条（69.46%）的边界/COLUMN 集合相同，2,578 条（30.54%）不同。仅 43 条 net 的精确 SLL node 集合相同；相同列并不代表相同 tile/channel。

下表使用真实器件物理 COLUMN，不使用 LAG tile 名中的 X 数字替代。SLR0–1 的最高列占用：带约束版 COLUMN 349 为 1,324 根（名义每列 1,440 根的 91.94%），无约束版 COLUMN 310 为 1,298 根（90.14%）。这一列级统计不能代表具体 tile 或普通互连资源的局部拥塞。

| 物理 COLUMN | SLR0–1 无约束 | SLR0–1 带约束 | SLR1–2 无约束 | SLR1–2 带约束 |
| ---: | ---: | ---: | ---: | ---: |
| 71 | 246 | 175 | 0 | 0 |
| 110 | 724 | 788 | 0 | 0 |
| 156 | 424 | 497 | 19 | 18 |
| 189 | 109 | 112 | 40 | 35 |
| 228 | 218 | 353 | 128 | 115 |
| 275 | 542 | 575 | 90 | 90 |
| 310 | 1298 | 1155 | 38 | 55 |
| 349 | 1267 | 1324 | 6 | 11 |
| 438 | 852 | 710 | 24 | 24 |
| 476 | 644 | 634 | 130 | 124 |
| 515 | 508 | 412 | 53 | 57 |
| 549 | 185 | 214 | 55 | 45 |
| 595 | 228 | 247 | 16 | 25 |
| 629 | 129 | 171 | 0 | 0 |
| 673 | 381 | 386 | 0 | 0 |
| 713 | 109 | 115 | 0 | 0 |

## 关键路径观察

带约束版最差路径从 `pidcu_loc_fu_1102_reg[0]` 到 `tmp_3_reg_17834_reg[19]`，在一个周期内经过 **SLR0→SLR1→SLR0**。数据路径 10.476 ns，其中逻辑 0.443 ns、布线 10.033 ns（95.771%）。两条跨 die net 的延迟分别为 6.536 ns（fanout 4,127）和 3.445 ns（fanout 1）。

其 SLL 位置存在明显差异：

| net（省略公共层次） | 带约束版 | 无约束版 |
| --- | --- | --- |
| `pidcu_loc_fu_1102[0]` | COLUMN 189；`LAG_LAG_X31Y296/UBUMP14` | COLUMN 515；`LAG_LAG_X96Y265/UBUMP11` |
| `LoopRows/.../dout[19]_INST_0_i_19_n_3` | COLUMN 349；`LAG_LAG_X69Y292/UBUMP23` | COLUMN 515；`LAG_LAG_X96Y269/UBUMP0` |

无约束版最差路径已经变为 `tmp_3_reg_17834_reg[35]` 到 `rows_162_U/ram_reg/DINBDIN[3]`，数据路径 9.645 ns，其中布线 9.567 ns，WNS +0.060 ns。默认 summary 的最差路径不是同一条，不能用两组最差路径延迟直接计算上述某一条 net 的改善。

本次固定方案没有减少最终 SLL 总数，也没有带来时序收益。下一步若要定位原因，应在相同端点锁定属性下比较 SLL 位置和分组拓扑；当前结果还不足以把差异只归因于某一个因素。

## 路径审计与只读诊断

原审计通过 1,842 条接收端记录后报本地 LUT 输入不可达；这不是 route_design 失败。错误来自审计将已使用的双向 PIP 按单一方向遍历。具体接收端为 `LoopRows/.../dout[43]_INST_0_i_93/I2`，位于 `SLICE_X38Y67/B6LUT/A3`，对应 net 为 `pidcu_loc_fu_1102[1]`。

保存后重开的 DCP 中，该 net 的 ROUTE_STATUS 为 ROUTED。实际图有 13,319 个 node、13,318 个 PIP；原方向遍历只到达 13,310 个 node。允许**已用、非定向** PIP 双向通行后，13,319 个 node 全部可达。报错输入的路径经过反向使用的 `INT_X24Y66/INT.INT_NODE_IMUX_18_INT_OUT0<<->>BYPASS_E14`，路径中没有 SLL，符合本地接收端分组。器件属性依据：[AMD UG912：PIP / SITE_PIP](https://docs.amd.com/r/en-US/ug912-vivado-properties/PIP-or-SITE_PIP)。

补充诊断 helper 只修改图遍历，不改变导出器、FIXED_ROUTE、cell 或 DCP，也没有再次运行 route_design。不同 SLL 序列汇合仍会被拒绝；小型图负对照已验证这一点。原审计失败日志保留，未改写成成功。

补充全量审计结果：**8,442 条 net、280,438 条逻辑接收端记录全部通过**，包括本地 156,106 条、远端 124,332 条，0 条不匹配。每条 net 的实际 SLL 集合与计划相同，各接收端的有序 SLL 路径符合其分组，本地接收端预期路径为空。

补充审计本身用时 3692.876 秒；包含 DCP 重开、索引和计划恢复的独立进程墙钟为 3848.180 秒。这是额外的只读验证开销，**不计入前表的 route_design 或 AMF 布局时间**。正式 helper 尚未合入这一诊断修正，后续复用原审计器仍需修正双向 PIP 遍历；本次未修改正式 helper，其 SHA-256 与冻结输入相同。

## 阶段耗时

单位为秒，route_design 包含默认流程内部的全部阶段；主验证进程墙钟另见结果表。补充审计单独统计。

| 阶段 | 无约束版 | 带约束版 |
| --- | ---: | ---: |
| open | 102.346 | 111.710 |
| initial_placement | 30.185 | 34.918 |
| route | 1725.547 | 1626.873 |
| route_status | 30.882 | 29.078 |
| timing | 77.702 | 40.295 |
| drc | 54.567 | 51.048 |
| placement_after | 32.052 | 31.670 |
| checkpoint | 55.124 | 54.272 |
| sll_usage | 20.383 | 20.521 |
| receiver_group_audit | — | 39.057 |

## 产物与复现位置

比较 ID：`getrf-e2-sll-route-pair-20261028-130616-093346`。

服务器副本：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo`。

基础提交：`64a4660c8fd628af6864a8594a90f3773cee7d86`；分支 `codex/e2-sll-free-terminal`。本轮没有执行或重建 AMF。实际运行 Tcl、Python 控制器和原审计 helper 冻结在比较目录 `inputs/`；Git 状态、参数、输入哈希和资源采样保留在 manifest/日志中。

输入 DCP 位于该副本下：

| 组别 | 输入路径 | SHA-256 |
| --- | --- | --- |
| 无约束 | `experiments/runs/getrf-sll-terminal-import-20261028-01/reports/getrf_before_sll.dcp` | `45678bc9b73ee269f15c2be9ccd270d7c22b1bc4e49a768937e3bc0077986800` |
| 带约束 | `experiments/runs/getrf-sll-terminal-import-20261028-06/reports/getrf_placed.dcp` | `9ccb0d061876d493aada21aae90ef7f17aaac7a1052d41e8a6597afc19df5521` |

两份输入哈希在运行后复核未变。输出 DCP 均已保存并通过 ZIP CRC；仅保存在服务器，没有下载到本地。

**无约束版已布线 DCP**

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-e2-sll-route-pair-20261028-130616-093346-sll-free/reports/route_attempt.dcp`

SHA-256：`78c526a79d5e775beb7730de37ac0a61243534ed38c72a52478fef71e181b819`。

**带约束版已布线 DCP**

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-e2-sll-route-pair-20261028-130616-093346-sll-fixed/reports/route_attempt.dcp`

SHA-256：`018f04273dfdca764bbe975b7b252589300a70fdef71555f7b6a08456106afb0`。

比较 manifest 与总状态：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/comparisons/getrf-e2-sll-route-pair-20261028-130616-093346`。

只读单连接诊断：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-e2-route-audit-inspect-20261028-134314`。

补充全量审计：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-e2-route-bidirectional-audit-20261028-134926`。

本地轻量证据：`AMFplacer3.0/experiments/evidence/20261028-getrf-e2-route-pair/results/`，包括两组正式报告、日志、输入脚本、SLL 明细及物理 COLUMN 统计。大型完整审计表默认保留服务器，本地同步覆盖统计与哈希。

最终聚合指标及完整审计表哈希保存在本地 `experiments/evidence/20261028-getrf-e2-route-pair/results/aggregate-results.json`，服务器为比较目录下的 `derived/results.json`。两份输出 DCP 哈希在最终汇总时再次复核一致。
