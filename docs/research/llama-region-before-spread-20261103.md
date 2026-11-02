# llama U250：区域纠正先于 spreading 预算

用户于 2026-11-03 批准先满足当前区域要求，再建立本轮 spreading 的 20-bin 预算，并重跑完整验收。修改仅在 `fixes/20261103-region-before-spread/repo` 独立副本进行，基于 `65e9bfab842868878700ce8f09da41305f63ac9c`；此前失败运行和日志诊断副本保留。

## 触发原因

真实 llama 的一个 LUT3 在首次 QP 后位于 Y=579.516479492，目标区域要求 Y≤479.25。bin 高度为 5，20-bin 半径仅允许移动 100；进入区域至少需要 100.266479492。以未纠正的 QP 位置为预算中心时，两项要求没有交集。详细诊断位于父任务 `diagnostics/20261103-spread-budget/`。

## 修复

`GeneralSpreader::prepareRoundLimit` 在所有资源扩散之前调用既有区域裁剪，随后同步单元/宏成员及密度网格，再建立共享位移预算。GlobalPlacer 只准备一次；CARRY、MUX、LUT、FF 继续共享同一个预算和激活状态。独立构造且未传入共享预算的 spreader，也在建立私有 die 视图之前完成准备。

固定与锁定 PU 继续跳过区域纠正。宏成员偏移、器件边界、硬 RP、同 die 写回保护与 200-bin 区域面积上限使用既有实现。区域偏好按既有容量/几何规则刷新，之后在资源轮次之间不重新准备预算。区域裁剪未启用时保留原始位置作为预算起点。

20 bin 表示相对“区域纠正后位置”的 spreading 距离；不包含区域纠正位移，也不表示相对原始 QP 点的总位移上限。此语义变化是用户已确认的修复方案。后续独立宏合法化、打包和后端处理仍单独计时与审计。

## 记录

在 `BoundaryReportDirectory` 中新增：

- `spreading-region-correction-summary.tsv`：准备轮次、阶段、迭代、被纠正 PU 数、最大/累计纠正距离（bin）、准备耗时，明确 `budget_origin=post_region_correction`。
- `spreading-region-corrections.tsv`：详细诊断启用时记录每个移动 PU 的前后位置、dx/dy 和纠正距离；不将这部分计入 spreading 位移预算。

## 验证与重跑

扩展 `checkCrossDieSpreadLimitIntegration`，同时开启区域裁剪与预算：先重现旧顺序的空交集错误，再检查区域纠正后起点、跨轮共享预算、宏偏移、固定/锁定对象、同 die 保护和密度守恒。覆盖 SLR/HPIO 阶段及串行/并行写回。既有预算、位移、200-bin、die 内优先和硬区域兼容测试也执行。

测试与构建证据保存在副本父目录 `validation/`。完整实验必须在这些检查通过后单独派发，仍使用 3.332 ns、benchmark 原派生时钟、普通 Pro、`ProPrimitiveTiming=false`、`SllRegionPolicy=preserve`、AMF 12 线程和 Vivado 2026.1 四线程。DCP 只保存在服务器。

完整流程结果以新运行的 manifest、布线、DRC、时序及接口合同审计为准；本修复文档不预先声明完整布局布线成功或 QoR 改善。
