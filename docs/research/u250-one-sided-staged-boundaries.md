# U250 单向吸引与 SLR→HPIO 分阶段扩散

日期：2026-10-07。状态：代码、完整编译及局部原生回归完成；尚无本版本的完整 GETRF 布局布线结果。

## 实现位置与版本

仅修改独立副本：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo
```

- 分支：`codex/one-sided-staged-boundaries`。
- 实现提交：`b8e1adfb827105e3f1ac9e758e2b9e52ed296a09`。
- 冻结程序：`builds/build-20261007-193332-608178-08928ffa/build/AMFPlacer`。
- 程序 SHA256：`4731d5df373e3d44e9580fd9ac0cc973b1ab4803fad4cc43829138ae8184711e`。
- 构建目录和原始 manifest 标注父提交 08928ffa：快照发生在暂存最终源码之后、提交完成之前。已核对全部 3,424 个受 Git 管理的冻结源码文件，内容与 b8e1adf 一致。证据在 `experiments/evidence/20261007-one-sided-staged-boundaries/source-validation.json`，未改写原始构建记录。
- 正式项目原码 105 个保护文件哈希全部保持不变。历史冻结二进制和实验产物保留，未切换默认构建，未恢复定时检查。

## 单向吸引和原版强度

保留原来的分组/投票范围：SLR 需要 cluster 真实实例严格多数；在选中 SLR 内再按严格多数选 HPIO 侧。SLR 无多数则跳过；HPIO 平票则仅保留 SLR 选择，X 始终自由。没有改 DFS、长路径种子或聚类规模参数。

每个 PU 保存本次偏好生命周期内固定的方向 s。令当前位置为 u、目标中心为 c，有向距离为 d=s(c-u)。越过中心后 d≤0，后续 QP 组装不再为它插入该轴锚点，不重新计算方向而产生反向吸引。

- HPIO：目标左侧 s=-1，右侧 s=+1。
- SLR：原来在目标 die 下方的 PU，s=+1；上方为 -1；已经完整位于目标 die 内的 PU 不额外施加 Y 中心吸引。中间 die 可以同时接收来自上下两个方向的 PU，各自保留来源方向。
- 宏按完整单元计算，方向不改变成员相对偏移。

令 N 为原版使用的 PU 去重 net 集合大小，即 `getNetsSetPtr()->size()`，β 为原有调用传入的锚点权重乘偏好强度（当前偏好强度仍为 1）。恢复以下原版单向分支，取代上一版本的“距离绝对值 × pin 数”：

| 条件（按顺序判断） | 锚点权重 |
|---|---:|
| d≤0，或 N=0，或 β≤0 | 0 |
| d>6 | β·N^1.1 |
| 0<d≤6 且 DSPCritical=true | 0 |
| 3<d≤6 且 DSPCritical=false | β·N |
| 0<d≤3 且 DSPCritical=false | (d/3)·β·N |

保留现有 `updatePseudoNetForClockRegion(0.2 * pseudoNetWeight)` 的调用系数、3/6 阈值、1.1 指数以及 DSPCritical 开关。当前 GETRF 配置的 DSPCritical=true，意味着 6 单位以内不施加该锚点；不能把这一配置下的行为描述为全距离连续拉动。

这里的方向判断发生在每次 QP **组装时**；单次 QP 仍沿用原版二次锚点形式，没有替换为新的非线性求解器。d 使用对应轴的现有布局坐标单位，既不是 ns，也不是物理微米。没有额外增加 Y 强度倍数。

## 真正分阶段执行

| 阶段 | 生效吸引 | 腾空间方向 | 保留范围 |
|---|---|---|---|
| 1：SLR | 仅 Y 单向锚点 | 选中 die 内沿 X 扩展；此时不锁 HPIO 侧 | 选中 SLR 的 Y 范围 |
| 2：HPIO | 仅 X 单向锚点 | 选中 HPIO 侧沿 Y 扩展 | 继续保留 SLR 的 Y 范围，并限制到选中 HPIO 侧 |

一次偏好建立后，先做 SLR 的 X 向腾空间。第一轮正常全局布局执行原有 QP 和密度扩散，随后裁回完整 PU，完成 SLR 范围恢复，再准备 HPIO 的 Y 向腾空间。下一轮原有 QP 使用 HPIO 的 X 锚点。没有增加 QP 次数、全局迭代次数或收敛参数；临时 `GlobalPlacement_fixedCLB` 不触发阶段切换。重复调用切换函数不会重复扩展。

HPIO Y 扩展的上下界直接取**选中 die**的上下界，而非整颗 FPGA 高度。扩展比例仍沿用已有的迁入实例/内部实例比例，不调整该参数。第一阶段允许 X 在 die 内移动；第二阶段才限制 HPIO 侧。

## 扩散后的裁回

在 `GeneralSpreader` 将扩散位置与历史位置混合、处理位移上限**之后**，再将完整 PU 投影回选定范围；随后同步 cell 坐标和 bin。这样不会因混合步骤重新越界。正常全局扩散结束后还检查未被 overflow-bin 扩散器选中的偏好 PU。

投影取最近的区域内坐标，不把越界实例直接送到中心。宏边界包含所有成员相对偏移，并保留现有 0.25 坐标余量；同时与已有器件合法范围求交，避免最外侧 die 的半行模型余量将实例推出芯片。

目标区域装不下完整宏时拒绝该 cluster 的选择，并记录 `macro-does-not-fit-target`；固定、锁定单元不移动。偏好仍使用原有清除/重建生命周期，清除后不再裁回，不是永久区域锁定或新的 Pblock。

本次未加入外部 floorplan 的持续软约束，也未加入 SLL 连线容量优化；现有资源容量预留不能代替 SLL 容量检查。局部回归通过不等于 GETRF 的 SLRC-1、布线或时序已经改善。

## 关键代码

路径均相对于上述独立副本：

| 文件 | 职责 |
|---|---|
| `src/lib/HiFPlacer/placement/placementTiming/HierarchicalBoundaryPolicy.h` | 原版分段权重，d≤0 时停止吸引 |
| `src/lib/HiFPlacer/placement/placementTiming/BoundaryAwareClusterer_Paper.cc` | 固定方向、SLR/HPIO 两阶段腾空间、阶段切换 |
| `src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.cc` | 按阶段插入单一轴的 QP 锚点，按 net 数计算强度 |
| `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h/.cc` | 阶段状态、宏范围、区域与器件范围交集、裁回、生命周期 |
| `src/lib/HiFPlacer/placement/globalPlacement/GeneralSpreader.cc` | 在混合/限位后裁回，再更新 cell/bin |
| `src/lib/HiFPlacer/placement/globalPlacement/GlobalPlacer.cc` | 现有全局迭代内执行裁回和阶段切换 |
| `scripts/paper_boundary_config.py`、`src/app/AMFPlacer/main.cc` | 能力版本升为 schema 2，防止新流程误用旧双向策略二进制 |

原 095 锚点分支和对应扩散分支逐字节保持不变。旧 `region-gain` 分支保留。U250 坐标、近中远延迟系数/判据、边界延迟、STA、0.5 ns 交换筛选、初始与最终打包算法均未修改。

## 验证证据

证据根目录：`experiments/evidence/20261007-one-sided-staged-boundaries/`。

- 完整 AMFPlacer 和 partitionHyperGraph 编译成功。
- 最终真实 U250 模型原生回归 **15/15**：左右目标侧、来自上方、上下双接缝、HPIO 平票、SLR 平票、已经位于目标 die 内、正偏移宏、负偏移宏、宏放不下、最下/最上 die、最下/最上 die 的宏、实际 QP。
- 原生测试调用真实聚类、QP 组装/求解和 `GeneralSpreader` 生产 worker，覆盖扩散混合、位移限制、完整宏/cell 坐标一致性、临时 fixedCLB、阶段重复调用、清除后解除约束。检查两阶段无同时 X/Y 吸引，越过中心后 X 锚点为零。
- 对原版权重独立复算 **100 个组合**（d、N、DSPCritical），另检查双方向越过中心后权重为零。
- 旧 region-gain **7/7** 场景通过：SLR、HPIO、二维、内部、DSP、URAM、未寄存 DSP。该回归基于第一版构建；之后仅补充 paper 策略的器件边缘交集和对应测试，旧 region-gain 源码未变化。
- 构建能力配置单测 **4/4** 通过；schema 1 旧双向策略不得由本版入口静默当作 schema 2 使用。
- 最终原生结果：`experiments/preflight/20261007-one-sided-staged-boundaries-native-02/manifest.json`。
- 第一版原生结果和构建一并保留；没有改写此前测试或完整实验。

## 既有无 floorplan 对照的最终状态

该对照使用本次修改**之前**的冻结版本 08928ffa，于 2026-10-07 18:52:43 HKT 结束。AMF exit 0，5507.679 s；856,998 实例按原 LOC/BEL 完整导入。Vivado `place_design` 前置 DRC 报 SLRC-1：

- SLR0–SLR1 至少需要 35,027 条连接，容量 23,040；
- SLR1–SLR2 至少需要 37,402 条连接，容量 23,040。

布局优化和布线均未开始，不能给出本轮 routed WNS/TNS。原运行保持失败现场，未重启。其 manifest、阶段退出状态和 DRC 原文摘录见 `old-no-floorplan-failure.json`。这一结果不属于 b8e1adf 新实现的验证。

