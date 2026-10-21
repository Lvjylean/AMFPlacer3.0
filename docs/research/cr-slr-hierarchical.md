# CR 边缘锚点层次聚类（78 独立副本）

本副本从 77 派生，新增 `BoundaryClusteringStrategy=cr-slr-hierarchical`，与 `slr-hierarchical` 同级。默认仍是 `slr-hierarchical`；旧 `paper-hierarchical` 仅作为它的别名。未启用 `BoundaryAwareClustering` 时，新策略不启动聚类。

## 配置

```json
{
  "PhysicalBoundaryMode": "true",
  "BoundaryAwareClustering": "true",
  "BoundaryClusteringStrategy": "cr-slr-hierarchical",
  "BoundaryStageMode": "independent-max2"
}
```

`BoundaryStageMode=independent-max2|merged` 均支持；默认仍是前者。上例是配置片段，不是可独立运行的完整设计输入。

## 锚点与停止规则

- 保留原层次聚类、真实 cell 多数票、目标 SLR / HPIO 侧选择、容量预留、聚类预算和区域腾挪流程。
- Y：最底 SLR 选其上边界内第一行 CR 中心，向下牵引；最顶 SLR 选其下边界内第一行 CR 中心，向上牵引。中间 SLR 按参与投票的真实 cells 平均 Y 与目标 SLR 中线比较：平均值在中线以下选下边缘，以上选上边缘；完全相等时确定性选下边缘。
- 同一 cluster 共享平均 Y、所选 CR 行和方向，包括跨上下两侧的 PU，不按每个 PU 所处位置重新选边。与投票一致，真实 macro cells、fixed/locked cells 纳入平均，virtual cells 排除；fixed/locked PU 自身仍不被牵引。
- X：左侧目标使用 HPIO 左边紧邻的一列 CR 中心，向左牵引；右侧目标使用右边紧邻的一列 CR 中心，向右牵引。HPIO 侧投票未过半时继续只保留 SLR 决策，不增加 X 牵引。
- 每次 QP 使用单向距离：尚未到达锚点时采用原来的权重公式；到达或向目标内部越过锚点后该轴权重为零，不反向拉回。不是永久解除：后来再次回到锚点外侧，会在原阶段仍有效时恢复牵引。
- CR 中心取真实器件 CR 几何，不硬编码 U250 的列数、行数、SLR 编号或坐标。原始锚点在下一次重建聚类前保持；阶段切换、PU 移动和 preference refresh 不重算 cluster 平均值。
- 全 PU 的硬 Pblock / 增量区域、器件边界和 macro 范围仍优先。缓存中保存的原始 CR 锚点为 cluster 公共值；交给求解器的有效坐标沿用合法区域投影，必要时限制在该 PU 的合法交集内，不绕过硬约束。

## 与已有开关的关系

| 参数 | 新分支中的含义 |
| --- | --- |
| BoundaryWeakAttraction | 保留原来的弱牵引距离分段与强度；不决定锚点。 |
| BoundaryAnchorStopOnRegionEntry | true 时完整 PU 进入阶段目标区域即提前停止，可早于 CR 中心；false 时按 CR 锚点的单向规则判断。 |
| BoundaryRegionClipping | 仍裁到原 SLR / HPIO 偏好区域边界，不改成裁到 CR 中心。 |
| BoundaryStageMode | 保留独立最多两轮或共同作用的原阶段预算；独立模式进入 HPIO 阶段后关闭 Y 锚点。 |
| IncrementalCompilation | 独立开关，使用 77 的正确键名及旧拼写兼容。 |

普通路径、74q 静态缓存/局部引脚访问/clock guidance 修补、纵向拉伸关闭状态和所有既有实验配置保持。裁侧和腾挪仍可能影响布局；本次不是仅加一个无约束伪网。没有重新启用默认关闭的边界感知聚类。

当配置 BoundaryReportDirectory 时，新增 cr_cluster_anchors.tsv，记录 seed、cluster、PU、目标 region、cluster mean Y、CR 行列、原始锚点及方向。既有报告格式不改写。

新二进制声明 cr_slr_hierarchical_schema=1；Python 启动器在显式启用本策略时拒绝缺少该能力的旧二进制，避免静默退回原策略。

## 验证边界

本次仅独立构建、几何/配置单元验证以及小型合成网表的实际聚类与 QP 验证，未运行完整 GETRF / Vivado 布线实验，不据此宣称 SLL、时序或性能改善。构建与验证结果以本副本上级的 implementation-manifest.json 为准。
