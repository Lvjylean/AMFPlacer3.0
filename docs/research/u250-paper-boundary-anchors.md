# U250：按论文思路恢复分层边界聚拢

2026-10-07。仅在外部 floorplan 独立代码副本实现；正式项目原码未修改。状态：构建和针对性验证通过，尚未进行完整 GETRF 布局布线，不作 WNS/TNS 收益结论。

## 用户确认的规则

1. 对长路径相关 cluster，按真实实例数先选择严格超过 50% 的 SLR。
2. 只统计已选 SLR 内的实例，再选择严格超过 50% 的 HPIO 侧。最终矩形不必超过整个 cluster 的 50%。
3. SLR 没有严格多数时放弃本轮分配。HPIO 侧平票时保留 SLR 的 Y 方向软锚点，X 保持自由；后续 X 移动也不会自动取消这个 SLR 选择。
4. 恢复中心锚点和扩展腾空间，保持后续布局自由度、宏完整性及资源合法性。外部 floorplan 仅决定初始化。

## 实现与原文的对应

依据 AMF-Placer 2.0 论文 III-B、图 5/6、式 (1)-(3)。跨 HPIO 与跨 SLR 使用同一套“成组选择区域、中心软锚点、边界平行方向扩展”的机制。原来的 0.5 ns 相邻交换筛选保持不变；它不是本次修改的对象，也不能仅凭它过滤跨界候选就断定旧算法缺少跨界优化。

`BoundaryClusteringStrategy=paper-hierarchical` 启用新策略；省略或使用 `region-gain` 保留此前的候选投影/延迟收益策略。095 原流程不变。新策略不修改近中远延迟函数、拟合系数、SLR/HPIO 延迟惩罚、STA 或最终 packing/交换代码。

- 聚类重新使用逻辑路径长度排序、DFS 和上游前 10% 种子范围。默认组上限为 20,000 个真实实例，dense placement 时为 2,000；宏作为完整 PU，虚拟占位单元不参与多数投票。DFS 遵守已选 PU 集合与组大小限制，不复制旧 DFS 的重复起点/忽略 exceptionCells 行为。
- 使用已有 long-path threshold。显式诊断参数 `BoundaryPaperPathLengthThreshold`、`BoundaryPaperMaxCells` 可以覆盖阈值和上限；交付实验配置未覆盖它们。
- 固定端点/硬资源可参与组内投票，DSP/BRAM/URAM 不由本步骤迁移；可移动 LUT/FF/CARRY/LUTRAM/MUX 以完整 PU 获得锚点。仍由后续合法化安排具体 site/BEL。
- HPIO 侧已确定时，X 锚点位于该侧区域内部；SLR 已确定时，Y 锚点位于该 SLR 内部。宏偏移用于调整其可行中心范围。锚点是虚拟坐标，不投影到接缝附近的最近 site。
- 采用论文式 (1)：每个活动方向的伪网权重为 `beta * abs(position-anchor) * realPinCount`。beta 复用现有 `updatePseudoNetForClockRegion(0.2 * pseudoNetWeight)` 调度；新锚点两个方向均使用该表达式，普通连线的 y2xRatio 不变。恰好位于中心时牵引为零，偏离中心后恢复；进入区域并不删除偏好。
- 对 SLR 迁入，在目标 SLR 的各 HPIO 侧内沿 X 扩展居民；对 HPIO 迁入，在目标侧的该 SLR 内沿 Y 扩展居民。扩展比例为 `1 + Noutside/Ninside`，关于原范围中心扩展并平移/截断到物理区域。固定单元不动，宏不拆分，不把居民扩展过另一条边界。零分母/零跨度安全跳过。
- 区域容量按整个组原子预留；HPIO 平票时允许同组各 PU 保持自己的水平侧，并在刷新时按其当前 X 更新容量归属。容量是粗粒度资源预算，不代表已经完成 site/BEL 合法化或 SLL 容量检查。
- 临时 fixedCLB 阶段使锚点休眠，不删除偏好，解除固定后恢复。新策略不再因短期单边延迟收益而删除软锚点。偏好仍可因 PU 生命周期、容量/几何不可行、显式重聚类而失效；现有最终打包前的清理步骤保留。

注意：这是论文思路的二维适配，不是对原版固定 CR 列号和全部实现细节的逐行复制。它也不承诺所有 cluster 都能聚入同一 die。

## 入口和构建

服务器副本根目录：
`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo`

配置：`configs/experiments/getrf-external-floorplan-paper-boundaries-10ns.json`。
与原外部 floorplan 10 ns 配置相比，唯一配置差异为新增 `BoundaryClusteringStrategy=paper-hierarchical`。

冻结构建：`builds/build-20261007-113251-137401-bbfbd584/build/AMFPlacer`。
二进制 SHA256：`380fa8bf76e0b7a11b575a4e17be9abbcc3e6a6ab99c3675dad5ee28bf9705a8`。
构建由 `scripts/amf3.py build --jobs 8 --no-set-current` 生成，没有替换默认构建。
新增能力标记 `paper_hierarchical_boundaries_schema=1`；full-run 和 inspect 入口会拒绝不支持新策略的旧二进制，避免新键被静默忽略。

## 验证

最终冻结构建源码与副本源码逐文件一致。正式项目 105 个非第三方源码文件与修改前哈希一致。

- 原生规则检查：SLR 多数、条件 HPIO 多数、两层平票、非连续 SLR ID、扩展截断、中心牵引。
- 真实 U250 tile-columns-v3 资源模型下六个生产实现测试全部通过：联合选择、HPIO 平票、SLR 无多数、进入区域后保留锚点、宏结构保持、实际 QP 求解。同时验证临时固定/解除固定、平票后的自由 X 移动、QP 活动方向、固定端点不移动、容量预留不重复扣账。
- 旧聚拢模式七个原生场景通过：SLR、IO、XY、inside、DSP、URAM、未寄存 DSP；物理边界/容量原生回归通过。
- 启动配置检查 4 项、已有 full-flow 回归 22 项通过。

证据：`experiments/evidence/20261007-paper-hierarchical-boundaries/validation.json`。
新策略测试：`experiments/preflight/20261007-paper-boundary-native-v3/`。
旧策略回归：`experiments/preflight/20261007-paper-boundary-legacy-regression-v2/`。

开发中保留的失败：第一版测试调用了私有 assembler 方法，改为公开的 QP 组装入口后通过；第一次旧模式回归因副本没有旧器件归档而失败，随后使用正式项目的只读输入路径、仍在副本保存输出后通过。这些不是 GETRF 布局布线失败。

本次未启动新的 GETRF 完整实验或 Vivado，也未生成、修改或下载 DCP。对最终跨 SLR 次数、WNS/TNS、拥塞和运行时间的影响需由后续同条件实验验证。
