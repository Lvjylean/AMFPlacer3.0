# 器件物理边界 A–D 验收：U250 / GETRF

2026-09-27。A–D 的代码、专项测试及三组完整 GETRF 布局布线对照均已完成。三组均全量布通、DRC 错误/严重警告为 0、专用级联检查通过。**本例推荐显式使用仅边界延迟配置 `getrf-u250-physical-delay.json`；二维聚拢保留为实验选项，未替换全局默认或历史验证构建。** 这次完成的是多 die 物理结构适配，不代表课题三的整个增量编译器已完成。

服务器根目录 `/Projects/jinyang/workspace/AMFplacer3.0`，分支 `codex/device-physical-boundaries`。原批准方案见 [修改清单](device-physical-boundary-clustering-plan.md)，实现细节、修复与失败轮次见 [实施记录](device-physical-boundary-implementation.md)。

## 交付范围

| 阶段 | 已完成的内容 | 验证 |
|---|---|---|
| A | Vivado 全 site/tile/SLR/clock-region/I/O bank 导出；统一 RPM→AMF 坐标；架构分类规则与资源区域模型 | U250 346,675 sites、781,860 tiles；3 条 SLR 接缝、1 条贯穿 HPIO 带、8 个区域；新旧 fabric 解压内容哈希相同 |
| B | C++ 物理边界查询、统一延迟接口；真实边界取代新模式的固定 X 列经验项；布线后连接延迟抽样 | 原生时序 15 组 / 157 行；旧模式兼容、端点处理、代价去重与 SLR 消融；真实 routed DCP 抽样 |
| C | 类型化区域容量、预留/释放、关键路径分段、二维区域软目标、X/Y QP 吸引、摊开与打包回退 | 原生聚拢 7 组；真实 U250 跨时钟列打包测试；完整 GETRF 的打包剩余 PU 降至 0 |
| D | 同输入、同二进制、同约束的 control / delay / cluster 完整流程；阶段耗时、覆盖、级联、DRC、时序、跨界和位置保留审计 | 三组 AMF 与 Vivado 退出码均为 0；三份最终 DCP 已重新核验 SHA-256 |

额外修复了真实验证暴露的 RAMB36 虚拟占位重复计数、时序权重数值失稳、QP 稀疏矩阵装配性能、下游打包遗留的硬时钟列筛选及报告中的时钟标识过滤。最终 59 项 Python 测试通过；原生物理模型、QP 稳定性（含十万变量）、时序、聚拢与打包测试通过。生产 C++ 最后冻结后只修改了报告/验收 Python 逻辑，无需重跑布局布线。

## 对照条件与追溯

- 输入为同一 `data/reference/getrf-u250/post_opt.dcp`，SHA-256 `6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`；网表来自同一 DCP，并补齐时钟负载。
- AMF 与 Vivado 均为 **10 ns**；已逐组检查 `clocks.rpt` 的 `ap_clk 10.000`。Vivado 2024.2，器件 `xcu250-figd2104-2L-e`。
- 每组 AMF 8 线程、Vivado 4 线程，三组并行；时间为实际观察开销，不能作为独占 CPU/内存条件下的速度比。
- 三组共同启用 `TimingMaxEnhancement=1000`、`QPStabilityGuard=true`、`SLRBoundaryDelayNs=1.5`。新物理模式的 I/O 带代价为 0.5 ns。数值保护与物理系数分别记录。
- control 为旧固定 X 修正＋现有 SLR 惩罚＋共同数值保护；delay 为新物理边界延迟；cluster 在 delay 基础上启用容量感知二维聚拢。因此 control 不是此前 SLR=0 的历史全流程基线。
- 冻结二进制为 `builds/build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer`，SHA-256 `47e7ba44fac7ac0c5499bbdb2500b100a47323d1a0477cd0b9622659d579ac17`，生产/测试源码 `7a7b5d8b`。启动提交 `49ac3e58`；后处理时钟过滤修复 `7ba71b3b`；最终 QoR 汇总逻辑 `0709ab8d`。
- 比较目录 `experiments/comparisons/getrf-physical-20260927-063459-803375/`；最终证据 `experiments/evidence/device-physical-getrf-20260927/manifest.json`。原始汇总与时钟审计报告均保留修订前版本，没有覆盖失败实验。

## 完整流程结果

| 指标 | control | delay：仅边界延迟 | cluster：加二维聚拢 |
|---|---:|---:|---:|
| WNS（ns） | −0.137 | **+0.003** | −0.059 |
| TNS（ns） | −1.708 | **0** | −0.072 |
| setup 违例端点 | 36 | **0** | 2 |
| WHS（ns） | +0.021 | +0.019 | +0.023 |
| hold / pulse-width 违例端点 | 0 / 0 | 0 / 0 | 0 / 0 |
| 完全布通 / 可布线网络 | 926077 / 926077 | 928336 / 928336 | 929758 / 929758 |
| DRC 错误 / 严重警告 | 0 / 0 | 0 / 0 | 0 / 0 |
| 硬资源级联违规（检查 12183 对） | 0 | 0 | 0 |
| SRL 级联违规（检查 325 对） | 0 | 0 | 0 |
| AMF 已分配 / 输入 cell | 856998 / 856998 | 856998 / 856998 | 856998 / 856998 |
| 布线后 AMF LOC+BEL 精确保留率 | 99.8028% | 99.7923% | 99.7819% |

各组导入时有少量位置被 Vivado 拒绝，已有后端完成补放；最终所有 AMF 输出 cell 均存在且有位置。导入时各有 2 对 SRL 级联缺少合法位置，布局及布线后均为 0。三个阶段分别保存审计，不把 AMF 输出完整等同于 Vivado 导入完全合法。Vivado 物理优化可改变额外原语与可布线网络数，三组不要求这些派生数量相等。

| 时间（s） | control | delay | cluster |
|---|---:|---:|---:|
| AMF 进程（含输出与退出） | 5531.142 | 5467.488 | 6708.648 |
| Vivado 读取 DCP | 93.639 | 89.565 | 78.439 |
| Vivado 导入 AMF 布局 | 1744.825 | 1750.966 | 1845.386 |
| Vivado place_design | 1091.598 | 944.370 | 981.574 |
| Vivado route_design | 3548.440 | 4762.925 | 6807.927 |
| 边界时序抽样（诊断开销） | 359.330 | 242.687 | 233.255 |
| Vivado 进程合计 | 7322.094 | 8237.224 | 10329.046 |
| 端到端实测（含审计/后处理） | 12941.096 | 13791.431 | 17112.490 |
| 端到端（min） | 215.7 | 229.9 | 285.2 |

本轮复用已导出的网表，表中不包含首次 DCP→AMF 网表导出的时间。端到端还包含检查、报告、保存 DCP 和诊断抽样；不能只拿 AMF 进程时间宣称全流程加速。

## 边界优化产生了什么变化

布线后按相同的 2,664,613 条数据 driver-sink 连接及实际 cell site 统计：

| 几何指标 | control | delay | cluster |
|---|---:|---:|---:|
| SLR 接缝穿越次数 | 168352 | 148839 | 173747 |
| I/O 带穿越次数 | 91321 | 109638 | 83761 |
| 至少跨一种边界的连接数 | 229122 | 224907 | 221680 |
| 最差 200 条 setup 路径中跨 SLR 的路径 | 175 | 187 | 125 |
| 最差 200 条中有 SLR 往返的路径 | 69 | 59 | 8 |
| 最差 200 条平均 SLR 穿越次数 | 2.290 | 2.125 | 0.785 |
| 最差 200 条中跨 I/O 带的路径 | 27 | 21 | 72 |
| 最差路径 SLR 穿越 / 往返次数 | 5 / 2 | 5 / 2 | 0 / 0 |

二维聚拢使本轮关键路径样本中的 SLR 往返明显减少，最差路径也转移至同一 SLR 内的 BRAM 输入路径；但整体 SLR 穿越次数并未最少，I/O 跨界关键路径更多。三组的最差 200 条不是同一组端点，统计是各自结果的观察，不能当作逐路径因果配对。几何计数也不是 SLL 使用量或实际线路轨迹。

cluster 相对 control 的 WNS 改善 0.078 ns、TNS 改善 1.636 ns，但没有优于 delay。相对 delay，cluster 的 AMF 耗时约增加 22.7%、布线耗时约增加 42.9%、端到端约增加 24.1%。因此功能验收通过，不代表二维聚拢已经成为最佳策略；不将它升为默认。delay 在本例达到当前约束，但余量只有 3 ps，尚无跨设计/多次运行的稳健性证据。

三组仍报告 OOC 时钟来源警告；没有虚构 `HD.CLK_SRC`。以上是现有 OOC 设计与给定约束下的结果，不替代板级实现验收。SLR 1.5 ns、I/O 0.5 ns 仍为可追溯的启发式系数；未加入 SLL 容量或拥塞估计，局部硬 IP 与未知类型不自动视为贯穿障碍。

## 产物与使用入口

最终 DCP 均只保存在服务器，以下路径相对于 `/Projects/jinyang/workspace/AMFplacer3.0/`：

| 组 | DCP |
|---|---|
| control | `experiments/runs/getrf-u250-full-20260927-063459-912642/reports/getrf_routed.dcp` |
| **delay（本例推荐）** | `experiments/runs/getrf-u250-full-20260927-063459-913333/reports/getrf_routed.dcp` |
| cluster | `experiments/runs/getrf-u250-full-20260927-063459-912759/reports/getrf_routed.dcp` |

三份 DCP 的 SHA-256 分别为 `13a2d5c56b43c2fa1f1d63cc9935e1eb949bbe7be05cb1b90b9cb090f9460d44`、`b783fb2ff743f810f9a7c8d106ac39226ec53ea2decfabc560b303d1c48cadb5`、`7381d1fbfdb720c44604cacf6d1bacc9664f9d3d60177abcc5c00136685c6cb1`；已核对实际文件，非预计输出路径。

若要再运行本例推荐模式，在服务器项目根目录执行：

```bash
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-physical-delay.json \
  --binary builds/build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer
```

这会创建新实验；查询本轮结果无需重跑。完整三组比较入口为同一冻结二进制配合 `amf3.py compare-boundaries --parallel 3`。必须保留共同数值保护参数，旧的无保护时序权重在本任务的失败实验中出现过 NaN；保留旧行为入口用于追溯，不将其描述为本轮已验证配置。
