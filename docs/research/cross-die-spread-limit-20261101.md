# 跨 die 扩散：按网格尺度的整轮位移限制

## 范围与参数

本独立副本基于 `sll-增量合并与加速` 的 `6c0557f5c55db2da1f5498082f909033e0a54106`，已包含修正后的 SLL 输入段 L 表达。只修改扩散写回的距离限制，不改 QP/SLL 线长、数量、τ、容量模型或最终宏合法化策略。

新增 `CrossDieSpreadMaxBins`，本副本缺省及常用配置为 `20`；`0` 恢复既有扩散限幅行为。负数、非有限数和混有单位的字符串拒绝。只有具备物理 SLR 边界且 `DieFirstSpreadingEnable=true` 时启用。本功能不依赖 SLL 路由约束导出开关。

以本轮 QP 后、所有资源扩散之前的 PU 锚点 \(p\) 为参考，原扩散目标（包括既有位置混合）为 \(q\)，使用未被 die 接缝切分的标称网格宽高 \(b_x,b_y\)：

\[
r=\sqrt{((q_x-p_x)/b_x)^2+((q_y-p_y)/b_y)^2},\qquad
q'=p+\min(1,20/r)(q-p).
\]

零位移保持不变。仅超限计算平方根；斜向移动沿原射线截短。当前方形网格边长 5、2.5、2 对应二维位移半径 100、50、40 个 AMF 坐标单位。20 是距离半径，既有 200-bin 扩散区域面积预算不变，也不强制每次移动满 20 bin。

## 整轮预算与写回

同 die 区域优先策略保留。某 PU 首次参与已放开 die 边界的跨 die 搜索区域后，启用本轮预算；同一 PU 同时属于 local/cross 区域时沿用 local 优先。搜索获准不等于最终一定跨界，参与跨 die 搜索区域的 PU 即便最终仍在原 die，也受限幅。

激活持续到本轮 CARRY、MUX、LUT、FF 等所有资源步骤结束，后续 local 步骤也不能重新起算。下一次 QP 后才刷新参考。激活 PU 保留原 forget-ratio 混合，替换旧的历史 `lastSpread`/10 单位限幅，避免早期不开启和后期旧上限遮蔽新参数的问题；未激活 PU 保持原策略。固定或锁定 PU 不移动。

以整个 PU/宏块锚点为单位移动，成员偏移不变。扩散内部的器件范围、硬区域、区域偏好和 local-die 保护执行后再检查最终锚点。若合法投影使径向截断超限，依次尝试前次位置及本轮原点的合法投影；仍不满足则明确失败，不默默扩大距离或放宽硬约束。额外的后续 `clipPaperRegionLocations`、独立宏合法化/打包不属于本限幅范围；本补丁不能保证最终合法化没有额外迁移，也没有修改 `setFixed` 条件。

集成测试另发现原 `transferCellBinInfo` 的 Y 参数为 `int`，会截断小数坐标，在 2.5 大小的网格中造成 bin 错配；本副本将该参数修正为 `float`，保留小数。该修正作用于所有调用方。

限幅后重新更新每个成员的实际 bin、坐标和 die 分片视图，再统计剩余溢出。没有真正迁出的对象不虚报释放容量；原最多 10 次内部处理和 GP 迭代预算不变。

## 开销与验证范围

每轮保存 PU 参考坐标及激活标记；复用 local-PU 集合，避免为新检查重复构造。对参与写回的激活对象进行常数次距离运算，只有超限对象额外投影和更新 bin。不增加候选位置搜索、QP 求解或反馈式试跑。日志每轮汇总半径、网格、激活 PU 数、限幅和候选拒绝次数。

单次计算开销预计很小，但没有声称总体 AMF 时间不变：限制位移可能影响密度收敛和合法化，仍需真实布局实验验证。当前任务只构建并进行小型回归，不自动派发完整 GETRF 或 Vivado。

测试目标：`checkCrossDieSpreadLimit`、`checkCrossDieSpreadLimitIntegration`；同时回归已有 `checkSpreadDisplacement`、`checkSpreadRegionLimit`、`checkDieFirstSpreading` 和 `checkDieFirstSpreadingIntegration`。二进制能力键为 `cross_die_spread_round_limit_schema=1`，旧二进制不能仅换配置冒充实现该规则。验证结果及构建身份另存本轮 provenance 和 evidence。

## 本轮验证结果

- 完整 AMFPlacer / partitionHyperGraph 构建成功；未切换 `builds/current`。
- 新单位测试及真实 U250 四单元写回测试通过，覆盖串行/并行、宏成员偏移、固定对象、跨资源共享参考、旧 10 单位限幅替换及容量守恒。
- 既有位移、200-bin 区域预算、die 内优先和单体/宏写回回归通过，L 分段语义回归通过。
- 构建目录：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261101-cross-die-spread-limit/repo/builds/build-20261101-015106-331245-6c0557f5`。
- 3533 个源码文件与冻结构建逐文件同哈希。
- 真实 GETRF 布局质量、总耗时及后续布线尚未验证。
