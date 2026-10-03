# R09 / R10 完整实验对照

两轮均在 GETRF/U250 的 10 ns 约束下完成。正式时序、全量布线、严格导入、DRC 和级联验收通过。日期与时间均为 UTC+8。

| 指标 | R09 | R10 |
|---|---:|---:|
| 日期 | 2026-09-28 | 2026-09-29 |
| 共享 / SA 有效比例 | 0.71 / 0.71 | 0.4 / 0.32 |
| WNS | +0.020 ns | +0.036 ns |
| TNS | 0 ns | 0 ns |
| setup 违例端点 | 0 | 0 |
| AMF 耗时（实际布局） | 未单独计时 | 35.49 分钟 |
| AMF 进程墙钟 | 64.89 分钟 | 37.55 分钟 |
| Vivado placement | 3.05 分钟 | 2.88 分钟 |
| Vivado routing | 27.71 分钟 | 26.42 分钟 |
| 布局导入 | 10.64 分钟 | 10.21 分钟 |
| **总墙钟** | **119.50 分钟** | **90.59 分钟** |
| 重布线次数 | 2 | 3 |
| SLL | 23,373 | 10,070 |
| HPWL | 9,731,280.399 | 3,190,300.817 |

AMF 实际布局采用 profiling 主线程排他功能时间，扣除输入、输出、诊断、验证、初始化和清理，包含布局内部调度与并行等待。R09 未开启相同 profiling，不能从进程总时间推造其纯布局耗时；新增进程墙钟一行保留可追溯的历史数据。
重布线次数按 Phase 5 Rip-up And Reroute 中已完成的 Global Iteration 统计：R09 为 0/1，R10 为 0/1/2；不包含 Initial Routing 和后续 Hold Fix。每轮开始与结束标记均已核对。
SLL 为布线后 Vivado Total SLLs Used，按边界的实际资源使用量相加，不是唯一跨 SLR net 数。
HPWL 为 AMF 最终加权 HPWL。R09/R10 坐标映射和权重不同，不能直接计算线长改善百分比。
总墙钟按 manifest.started 到 status.finished 计算，包括工具、适配、检查、报告、DCP 写出和收尾；两轮均复用 DCP→AMF 网表缓存，不含首次网表导出成本。R10 启用 profiling，插桩与服务器负载影响未伪造扣除。

## 实验、DCP 与配置地址

### R09

- 运行目录：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-190947-261685
- 最终 DCP：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-190947-261685/reports/getrf_routed.dcp
- 实际执行配置快照：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-190947-261685/config.json
- 配置 SHA-256：38816b85a14e6771a60380f5ee8745c74f7b05512f9cf4ebebf74c214eb14bcf
- AMF 构建：/Projects/jinyang/workspace/AMFplacer3.0/builds/build-20260928-002048-349585-d534237b/build/AMFPlacer
- DCP SHA-256：5260e6ee54eb773e8d55ea6529d59fdb058fad83d592859582f76be5ad36385b
- 开始：2026-09-28T19:09:47.638634+08:00
- 结束：2026-09-28T21:09:17.595603+08:00

### R10

- 运行目录：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380
- 最终 DCP：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380/reports/getrf_routed.dcp
- 实际执行配置快照：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380/config.json
- 配置 SHA-256：c12d289990fbd6ad9cad24d05765b593c1d1e4f42b6c01ad329392dc9bfcaa1b
- AMF 构建：/Projects/jinyang/workspace/AMFplacer3.0/builds/build-20260929-132823-663969-c70df682/build/AMFPlacer
- DCP SHA-256：fa1973d8766aaebfd12d3381d78cacd2aaee94bf46cace793c1fbe54c8698d49
- 开始：2026-09-29T13:33:45.667718+08:00
- 结束：2026-09-29T15:04:20.919310+08:00

R10 的 full-run 启动参数配置文件：/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20260929-getrf-r10-review/r10-proposed.json。实际执行的 config.json 是入口解析该文件并填写本轮输出路径后的快照。R09 以运行目录 config.json 为准，避免当前常规配置被后续修改造成混淆。

## SLL 边界明细

| 边界 | R09 | R10 |
|---|---:|---:|
| SLR0 ↔ SLR1 | 15,198 | 9,538 |
| SLR1 ↔ SLR2 | 8,175 | 532 |
| SLR2 ↔ SLR3 | 0 | 0 |

## R10 Profiling 与适配计时

| 项目 | 秒 |
|---|---:|
| AMF 输入器件/网表 | 23.814 |
| AMF→Vivado 导出 | 0.742 |
| Python 格式转换/适配 | 38.575 |
| Vivado 布局导入 | 612.306 |
| AMF 诊断/中间输出 | 90.292 |
| Vivado DCP 打开 | 76.689 |
| Vivado 导入/布局/布线后审计合计 | 52.878 |
| Vivado 正式报告 | 166.499 |
| placed / routed DCP 写出合计 | 147.415 |
| 边界时序样本导出 | 248.211 |

AMF 内部子项已经包含在 AMF 进程墙钟中，不能重复相加。全部排他功能时间及包含时间见本轮 reports/profile_summary.md、profile_categories.csv、profile_functions.csv。

| Profiling 主要功能 | 主线程排他墙钟（分钟） |
|---|---:|
| 二分图匹配 | 20.481 |
| PU/网格/网络数据维护 | 4.144 |
| 最终 CLB 打包与 BEL 分配 | 2.378 |
| 过密区域扩散 | 2.336 |
| 时序图与 STA | 1.550 |
| 诊断、报告与中间文件输出 | 1.505 |
| 硬宏合法化 | 1.053 |
| QP X/Y 并行求解及等待 | 0.858 |
| CLB 合法化 | 0.672 |
| QP 目标构建与坐标回写 | 0.581 |

## 验收证据

R09：926,807/926,807 routable nets 全量布通；R10：928,390/928,390 全量布通；路由错误均为 0。两轮 DRC Error/Critical Warning 均为 0，12,183 条硬级联与 325 条 SRL 级联检查均无违规；856,998 个 AMF 指定 cell 严格导入且最终 LOC/BEL 保留。两份 DCP 哈希已重新计算并与 status.json 一致。

机器可读汇总：experiments/comparisons/getrf-r09-r10/summary.json。
