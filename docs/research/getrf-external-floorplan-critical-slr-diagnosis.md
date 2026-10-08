# GETRF 外部 floorplan 最终 DCP：关键路径跨 SLR 与代码诊断

结论：最差路径确实在 SLR0/SLR1 间往返五次，但没有发现“SLR 惩罚漏加”或“加密算子对 AMF 不可见”。当前实现的主要缺口是 **时序模型能看见边界代价，实际选址与局部修复却没有一致地优化这个代价**。主打包的所谓 HPWLChange 实际提前返回位移；宏合法化的 timingDriven 模式也只用位移；两处交换候选预筛又用包含 1.5ns 跨界惩罚的延迟与 0.5ns 比较，导致所有跨 SLR 交换候选被排除。CARRY 宏在后期局部优化中不能移动，进一步限制修复范围。

本报告以运行冻结源码及指定 routed DCP 为依据；只读诊断，没有改布局算法、系数、约束或 DCP，没有重新执行 place_design/route_design。正式原码未修改。

## 1. DCP 中到底发生了什么

目标：
`experiments/runs/getrf-external-floorplan-10ns-vivado2026-1-full-20261007-013540-091549/reports/getrf_routed.dcp`

以上路径相对于服务器独立副本：
`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo`。

DCP SHA-256：`6c7abb432efcf33f7585e73a92484e27e275d2c0da48ec8e05a7874246011227`。

重新用 Vivado 2026.1 只读打开 DCP，提取 200 条不同终点的最差 setup 路径，原生 SLR_CROSSINGS、slack、终点与原报告全部一致。不是全设计所有路径的抽样统计。

- 最差 slack −0.436ns，数据路径 10.414ns，其中逻辑 0.760ns、连线 9.654ns（92.7%）。
- 7 个组合逻辑级：CARRY8=1、LUT3=2、LUT4=1、LUT5=1、LUT6=2；这条最差路径没有经过 DSP48E2 原语。
- 整条路径位于同一个 `grp_subUpdate...fu_6054/.../dsub_64ns_64ns_64_5_full_dsp_1_U1561` 浮点减法实例内部，不是多个完整浮点算子首尾相接。
- 发射 FF → SUM LUT → ZERO_DET LUT → CARRY8 → DIST_SKEW LUT → 三个 SHIFT LUT → 接收 FF。
- 去掉连续重复的 SLR 后，序列为 **0→1→0→1→0→1**，共五次跨界、两次额外往返。
- 200 条路径中，30 条跨五次，112 条跨一次，58 条不跨；16 个负 slack 终点中，10 个跨五次（同一 dsub 实例），6 个不跨。不能把全部时序违例都归因于跨 die。

关键五条跨界连接如下。实测列为 NET_DELAY.SLOW_MAX（ps/1000），包含整条连接延迟，不是单独测量的“SLR 附加延迟”。

| 源 site → 目标 site | SLR | 扇出 | 实测 ns | AMF 公式复算 ns |
| --- | --- | ---: | ---: | ---: |
| SLICE_X12Y238 → SLICE_X14Y240 | 0→1 | 1 | 1.803 | 1.787 |
| SLICE_X12Y240 → SLICE_X12Y239 | 1→0 | 75 | 2.170 | 1.658 |
| SLICE_X12Y239 → SLICE_X12Y241 | 0→1 | 62 | 1.890 | 1.669 |
| SLICE_X12Y241 → SLICE_X9Y239 | 1→0 | 3 | 1.793 | 1.946 |
| SLICE_X9Y239 → SLICE_X12Y243 | 0→1 | 4 | 1.752 | 1.968 |

这五条连接合计 9.408ns。单元在 SLICE Y=238…243 的很小纵向范围内，却紧贴 Y=239/240 的 SLR 接缝两侧。几何上相邻，跨 die 的代价却高。

本次 AMF 导出 LOC/BEL 在 imported、placed、routed 审计中均为 100% 保留。因此可以将这种单元分布追溯到 AMF 输出；Vivado 负责实际路由延迟，但没有把这些单元重新摆成来回跨 die。

## 2. 惩罚确实存在，而且计入了所有五次跨界

[PlacementTimingOptimizer.h](../../src/lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.h) 第 113–169 行使用共同距离公式：

```text
net_delay = max(0.05ns, 原近/中/远距离项)
          + 1.5ns × 跨越的 SLR 边界数
          + 其他已确认物理边界项
```

实际启用 PhysicalBoundaryMode=true；物理模型接缝为 Y=239.5、479.5、719.5。路径的全部 X 坐标在 HPIO 带左侧，无 IO 带惩罚。相关 LUT/FF/CARRY pin offset 默认是 0；专用 PCIE offset 分支不涉及该路径。

[PhysicalBoundaryModel.cc](../../src/lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.cc) 第 142–165 行逐边判断 `(Y1 <= seam) != (Y2 <= seam)`。它每次调用都累加跨界，路径来回穿过同一条接缝会重复计费，没有只按起点/终点净跨界一次计算。

依据最终 site 坐标复算八条数据连接：
- 距离项合计约 1.930474ns。
- SLR 惩罚合计 **7.5ns**。
- 模型 net 延迟合计 **9.430474ns**。
- 独立连接 SLOW_MAX 合计 9.651ns；路径报告 net 延迟为 9.654ns，几 ps 差异涉及舍入及路径转沿，不将两者强制混同。

冻结的 getDelayByModel_conservative、crosses、penalty 函数体原文被独立编译，八条连接与 Python 复算最大差小于 7.9e−8ns；原文编译结果惩罚合计 7.500000074ns。这只核验公式、分段、截断、边界及浮点计算，不冒称完整历史 STA 重放；器件查询桩只替代 physical=true 分支里不影响结果的 CR plumbing。

总误差小不代表各连接都准确：扇出 75 的连接低估约 0.512ns，另两条连接存在高估，部分抵消。固定 1.5ns 没有建模负载、SLL 接入及路由拓扑，仍是粗模型；但“漏了五次跨界”不是此次解释。

## 3. AMF 自己已经认为该终点很差

原运行保存的 `reports/physical/amf_critical_paths.tsv`，同一终点 cell ID=303595：

| 阶段 | AMF rank（从 0 起） | AMF slack ns | AMF 回溯边数 | 跨 SLR 次数 |
| --- | ---: | ---: | ---: | ---: |
| amf-before-pack | 21 | −6.79767 | 20 | 6 |
| amf-final-packed | 25 | −5.61736 | 19 | 5 |

这直接排除了“AMF 根本没看到这个终点的跨界风险”的解释。局部优化有所改善，但最终仍留下严重的模型违例。

AMF 回溯的是 cell 级简化图上的路径，最终 19 条连接；Vivado 最差路径有 8 条数据连接。两者不是同一条时序弧路径，不能把 −5.61736ns 与 −0.436ns 相减当作模型预测误差。排序/路径精度也是限制，但不妨碍确认该终点在 AMF 内部已经很关键。

## 4. 具体代码缺口

### 4.1 主打包评分的“HPWLChange”实际上是位移，并没有候选跨界增量

[ParallelCLBPacker.h](../../src/lib/HiFPlacer/placement/packing/ParallelCLBPacker.h) 第 1938–1941 行：

```cpp
inline float getHPWLChangeForPU(PlacementUnit *tmpPU) {
    return fabs(tmpPU->X() - siteX) + y2xRatio * fabs(tmpPU->Y() - siteY);
    // 后面的真实 HPWL 计算因为上面的 return 不会执行
}
```

[ParallelCLBPacker_PackingCLBCluster.cc](../../src/lib/HiFPlacer/placement/packing/ParallelCLBPacker_PackingCLBCluster.cc) 第 557、613 行评分为：

```text
score = 0.40×net数 + 0.15×path长度 + 0.5×连接紧密度
      − HPWLWeight×“HPWLChange”
      − 0.2×LUT占用项 + 0.1×已有负slack项
```

已有负 slack 是当前时序状态，不是把 PU 试放到该候选 site 后重算的 slack。对于同一个 PU，“这一单元很关键”不能自动区分“放接缝这边”和“放另一边”哪个更好。位置变化项实际只看到几何位移，不会直接看到新增或消除的 1.5ns 跳变。

此外，连接紧密度统计跳过 unit 数大于 64 的 net。这不能解释为 STA 完全忽略高扇出，但意味着该项也不会充分体现长扇出连接的跨界代价。

### 4.2 宏合法化的 timingDrivenLegalize 不是候选位置的真实时序评价

[MacroLegalizer.h](../../src/lib/HiFPlacer/placement/legalization/MacroLegalizer.h) 第 787、819、844 行：timingDrivenLegalize=true 时直接返回

```text
|candidateX − currentX| + 0.4×|candidateY − currentY|
```

个别重载再除以宏中单元数。非 timingDriven 分支计算 HPWL 变化，也没有逐连接的候选 SLR 惩罚。因此“当前 STA 已增加 1.5ns”不代表宏匹配/列内 DP 的目标函数会对跨界候选增加同样代价。靠近接缝时，几何距离与时序代价尤其不一致。

### 4.3 两处交换预筛与 1.5ns 边界项冲突

[ParallelCLBPacker.cc](../../src/lib/HiFPlacer/placement/packing/ParallelCLBPacker.cc) 第 **903** 和 **1639** 行：

```cpp
if (timingOptimizer->getDelayByModel(nextX, nextY, curX, curY) > 0.5)
    continue;
```

任何跨一条 SLR 的候选，完整公式至少为 **0.05+1.5=1.55ns > 0.5ns**，因此这些交换候选必然被过滤，包括几何上非常接近、可能消除往返的候选。

这是“同一延迟函数同时用作代价和邻近性门槛”的兼容问题。该筛选不能简单地沿用单 die 的 0.5ns 含义。**并非所有跨 SLR 移动都被禁止**：前面的 shortest-path 候选搜索仍可跨界，但这两处交换修复通道确实被关掉了。不能据此断言放开筛选就一定有合法且获益的交换，仍要检查资源和所有受影响连接。

### 4.4 CARRY 宏锚点与有限候选范围限制了后期修复

ParallelCLBPacker.cc 第 600、723、883、1619 等行会排除包含 CARRY、LUTRAM、BRAM、DSP、URAM 的 PU。普通候选搜索范围也较小（如 shortest-path 的 5×displacementRatio，下限 1，且受 site 数/打包可行性限制）。

此路径发射 FF 并非独立可随意移动的 FF，而是属于 **CARRY 宏 3645**，位于 SLR0；中间 ZERO_DET 的 CARRY8 及其输入 LUT4 属于 **CARRY 宏 3655**，位于 SLR1。这些宏原有相对结构被保留；后期局部优化无法只把其中某个成员搬到另一侧来解决问题。

固定这两个宏时，至少一次跨界仍然存在；剩余往返需要周边逻辑有合适候选，必要时还需要更早阶段整体移动宏。当前只靠局部贪心/有限候选，加上跨界交换预筛，不能保证把 5 次收敛为 1 次。

### 4.5 边界引导是临时的，后期不是持续优化边界往返次数

[AMFPlacer.h](../../src/app/AMFPlacer/AMFPlacer.h) 第 371、379、400 行只在若干阶段执行物理区域聚拢；第 440 行在最终打包前清除 regionPreferences。末期 qp_region_targets 记录为 0；amf-final-packed 的 region_preferences 也为 0。

[WirelengthOptimizer.cc](../../src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.cc) 第 565–660 行会依据带边界代价的 slack 增强连接权重，但求解动作仍主要是几何拉近。两端隔缝贴得很近仍然可能有高边界代价；增加弹簧权重不等于增加一个能推动整段逻辑统一选边的离散动作。

当前没有持续的“整条组合路径少往返”的显式约束，也没有完整的跨阶段候选边界代价一致性。保持 floorplan 为软约束本身是正确要求；软约束不要求去掉时序目标或候选边界评价。

## 5. 与外部 floorplan 的关系

最差路径的 9 个真实单元全部属于外部分区 **S01**，不是跨分区宏的争议。直接匹配到的 6 行初始 PU 种子都位于 SLR1（Y=322…447），且 mixed_membership=0。其余单元属于复合 PU，不能把“没有按 cell 名直接找到种子”说成缺失。

最后两份全局布局快照也显示明显变化：FinalLUTFF-3.gz 中可观察的八个 LUT/FF 全部位于 SLR1；FinalLUTFF-4.gz 中已出现接缝两侧分布，例如源 FF Y=238、SUM LUT Y=238.403、DIST_SKEW LUT Y=238.228，而部分 SHIFT LUT Y>239.5。最终 site 分配又改变部分单元的侧别。这些快照只记录 LUT/FF，不能据此伪造完整历史 CARRY 坐标或整条路径的历史延迟。

因此，本次外部 floorplan 改变了优化轨迹，但并没有要求这条路径反复跨 die。主要问题是后续流程在接缝附近缺少一致的边界感知选址与有效的跨界修复动作。

## 6. 应怎样处理

不建议把第一步设为盲目提高 SLRBoundaryDelayNs。优先顺序应是：
1. 将“候选搜索/交换邻近性”与“完整时序评价”分开。邻近性使用几何或纯距离项；接受候选时仍计算完整边界代价，不能以此为由降低跨界惩罚。
2. 在宏合法化与 CLB 打包候选评价中，加入受影响连接的边界延迟增量或时序增量；保留现有合法性、资源和宏相对结构。
3. 为关键路径提供能跨接缝、考虑容量的软修复，必要时整体移动 CARRY 宏；不把整个加密算子或外部分区硬锁死。
4. 再诊断固定 1.5ns 对高扇出、方向及 SLL 接入的误差，讨论标定，而不是让一个更大的常数掩盖目标函数不一致。
5. 用同一冻结条件进行修复前后对照，核验 AMF 自身跨界数、DCP 实测时序和位置合法性。只读证据无法预言每项修复分别能改善多少 WNS。

额外源码观察：PackingCLBCluster::updateScoreInSite 在累加 totalNegSlack 前没有像 incrementalUpdateScoreInSite 那样显式清零。它是需要独立验证的评分状态问题；本次没有保存调用级状态，不把它归为已证明的主要根因，也没有因此修改算法。

## 7. 证据与限制

- [结构化诊断](../../experiments/evidence/20261007-external-floorplan-critical-slr/analysis.json)：逐连接公式、最终宏归属、阶段坐标、200 条路径、冻结源码一致性及原生表达式交叉验证。
- [直接重读 DCP 的路径表](../../experiments/preflight/20261007-critical-path-slr-095608/paths.tsv)。
- [Vivado 前 20 条完整时序报告](../../experiments/preflight/20261007-critical-path-slr-095608/top20_timing.rpt)。
- [只读 Tcl](../../scripts/diagnostics/inspect_external_floorplan_critical_paths.tcl) 与 [分析脚本](../../scripts/diagnostics/analyze_external_floorplan_critical_paths.py)。
- 第一次只读诊断使用了该版本不支持的 POINTS 属性，失败现场在 094803 目录；改用实际支持的 pin 查询及 SLR_CROSSINGS 后，095608 目录完成，exit_code=0。没有重跑布局布线。
- 判定来自实际冻结构建，不是仅看当前未核验工作树。所有引用的核心源码与该运行 build source snapshot 一致。
- 最差路径上的原语与连接都存在于 AMF 输入/输出。Vivado 文本中的 hidden 层级不代表 AMF 将整个算子当成不可见黑盒。
- OOC 约束及接口 delay 缺口沿用原实验；本报告结论针对当前已约束的内部 setup 路径。

