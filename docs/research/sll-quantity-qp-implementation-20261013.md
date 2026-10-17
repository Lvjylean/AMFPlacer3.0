# SLL 数量模型与 QP 接入：第一批实施

## 范围与基线

本次覆盖用户确定的第 1–5 步：冻结口径、数量模型、梯度验证、QP 接入、真实设计跨 SLR 闭环验证。空间需求、热点优化、虚拟 SLL cell 和 Vivado 固定路由接口不属于本次实现。

- 原始源码：eda099 `/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-srl-share4/repo`，提交 `e7f6ac93b76c0c798d126ee1d66eb326929f1447`。
- 独立实现副本：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01/repo`，分支 `codex/sll-quantity-qp`。
- GETRF / U250，用户目标周期 10 ns，8 个 AMF 线程，普通 Pro，显式 `ProPrimitiveTiming=false`。
- 保留原 U250 坐标、`y2xRatio=0.4`、时序边界系数、SRL 共享、弱牵引等普通 Pro 设置。
- 保留独立 SLR 最多 2 轮及后续 HPIO 阶段预算；配置 `GlobalPlacementIteration=30` 不代表每个内部阶段都运行 30 轮。各阶段实际预算及退出原因从日志提取。
- 构建、运行采用 `scripts/amf3.py` 标准入口。构建保存源码快照/补丁/哈希；每组运行保存配置、输入哈希、工具版本、时间、退出状态。
- 当前真实设计检查执行完整全局布局前缀，包括中间增量打包和宏合法化，在最终 CLB 打包前退出。不能据此宣称完成最终布局、Vivado 布线或 WNS/TNS 验证。

## 1. 数量和网络口径

读取已有物理结构元数据的 SLR region 和 active SLR boundary，两者必须相互一致。当前实现明确限制到纵向 `1×4` 拓扑。U250 的真实接缝是 `239.5 / 479.5 / 719.5`，坐标域是 `[-0.5, 959.5]`，不是等分 cell 数或猜测 die 高度。

整数需求为 `S_n = max s(pin) - min s(pin)`。恰在接缝上的 pin 属于下方 SLR，与物理 region 的 `y0 < y <= y1` 约定一致。逐接缝统计求和必须等于 S；多 sink 在同一侧不会按 driver–sink 对重复计算。

映射来自 `DesignInfo::getNets()`，不能仅用 `PlacementInfo::getPlacementNets()`：后者省略内部只有一个 PU 的网络。每个 pin 使用 PU 的实际 anchor Y，加宏内 cell 偏移，再加 cell 内 pin 偏移。同一 PU 上具有不同偏移的 pin 均保留，梯度最后累加回该 PU。

统计排除常量网络和 AMF 标记的 global 网络。global 标记沿用现有输入，不能一概解释为“已证明使用专用时钟布线”。缺失映射、越界网络和优化过滤网络分别报告。

优化默认使用至少两个 pin、恰有一个 driver 的普通网络；`SllQuantityMaxFanout=0` 表示不限制扇出。未纳入优化的普通网络仍参与离散 S 统计。每个优化网络权重当前为 1，未增加新的时序加权。

S 使用 AMF 导入网表中可观察的 pin 集合；例如缺少显式 driver cell 的外部输入网络，其统计只反映已知 sink 的跨度，不能补出未知外部端点的位置。报告同时给出全部观察网络的 S 和优化网络的 S_optimized，后者的网络集合与 WASLL 一致。

越界网络不计入有物理依据的整数 S，但保留连续代理项，避免通过越界让代价消失；候选检查还禁止越界网络数增加。S 是布局拓扑需求，不能当作布线后的物理 SLL 去重占用 U。

## 2. 连续代理与梯度

令 `z_p = sum_b sigmoid((y_p-B_b)/tau)`，再对各网络计算：

\[
\widetilde S=\sum_n\left(\frac{\sum_p z_p e^{z_p/\gamma}}{\sum_p e^{z_p/\gamma}}-\frac{\sum_p z_p e^{-z_p/\gamma}}{\sum_p e^{-z_p/\gamma}}\right).
\]

SoftFloor 宽度 tau 和 WASLL 温度 gamma 是两个独立参数；当前验证固定 `tau=30`、`gamma=0.25`，尚未加入退火调度。指数计算先平移，避免小 gamma 下溢出。

设两个 softmax 权重为 `a_p`、`b_p`，均值为 `mu+`、`mu-`，则

\[
\frac{\partial A_n}{\partial z_p}
=a_p\left(1+\frac{z_p-\mu_+}{\gamma}\right)
-b_p\left(1-\frac{z_p-\mu_-}{\gamma}\right),\quad
\frac{\partial z_p}{\partial y_p}=\sum_b\frac{\sigma_b(1-\sigma_b)}{\tau}.
\]

固定/锁定 PU 的 pin 仍参与目标，但不生成该 PU 的可移动自由度梯度。OpenMP 按 net 静态分配，每个线程使用自己的 PU 梯度数组，最后按固定线程顺序归约，不使用浮点原子累加。内存开销约为 `8 × threads × PU_count` 字节的梯度缓存；不同线程数允许浮点求和次序误差。

## 3. QP 接入和候选检查

新增文件位于服务器独立副本：

- `globalPlacement/SllQuantityModel.h`：不依赖 placer 的数学内核。
- `globalPlacement/SllQuantityOptimizer.{h,cc}`：网表映射、QP 适配、候选检查、阶段诊断。
- `placementInfo/SllClipTrace.h`：扩散/裁侧内部区域投影的被动计数。
- `tests/check_sll_quantity.cc`：独立数学与合成跨界检查。

原 QP 使用 `0.5 yᵀAy + bᵀy`。在每次完整 B2B/时序/伪网建模之后，以真实 PU 位置 `y0` 为展开点，加上

\[
\lambda\left[g^T(y-y_0)+\tfrac12\rho\|y-y_0\|^2\right],\qquad
A\leftarrow A+\lambda\rho I,\quad b\leftarrow b+\lambda(g-\rho y_0).
\]

只修改可移动 PU 的 Y 系数；X 的 SLL 梯度为零。正对角项用于局部稳定，不把非凸目标的完整 Hessian 强行塞入线性 CG。一次 CG 求解中 lambda、tau、gamma、梯度和展开点固定。

实现顺序：

1. 原有 QP 建模和 warm start 加载。
2. 快照实际 PU 坐标，计算连续目标/解析梯度，保存原始 X/Y QP 对候选差值的计算数据。
3. 添加局部线性项和正对角项，沿用现有 X/Y 求解线程及 QP 稳定检查。
4. 从求解器读出候选，在临时坐标上应用原有位移限制与器件区域投影，限制新增路径每次 Y 位移不超过 `SllQuantityMaxStep`（浮点舍入容差除外）。此时不写回 PU。
5. 用“冻结的原 X/Y QP + lambda × 真实 WASLL”检查候选，最多 16 次减半回溯；不满足则回滚本次坐标。采用差值公式，避免两个巨大绝对目标相减。
6. 接受后只写回一次 PU，正确记录 `lastAnchor`；拒绝时不修改 PU 和锚点历史。随后用实际坐标更新求解器缓存，避免下一次 QP 使用投影前的旧坐标。

因此保证的是接受步的合成目标不增加，**不保证每一步的整数 S 或单独 WASLL 都下降**。正对角稳定项和位移上限也会影响轨迹；数量组的结果属于整个接入方案的效果，不能仅归因于某一个梯度项。

`SllQuantityEnable=false` 或权重为 0 时跳过系数修改与候选路径。阶段外的初始固定 CLB QP 不启用数量优化。诊断开关本身只读取布局并记录计数；恢复原版行为还要求 `SllRegionPolicy=preserve`。

## 4. 区域偏好与硬约束

默认 `SllRegionPolicy=preserve`，不改变 Pro 的区域偏好。另提供显式实验策略 `release-single`：在 GP 迭代入口及 timing 步之后，释放未固定、未锁定、未打包的普通 LUT/FF 的 **启发式区域偏好**。不释放 LUTRAM，不拆宏，不改变 site、级联、固定位置、时钟资源和器件边界约束。

该策略移除所选单体 PU 的整个偏好项，故同时释放该项原有的 X/HPIO 和 Y/SLR 偏好；不是“只放开 Y 的硬围栏”。宏的偏好仍保留。被释放数量在阶段报告中记录，后续区域容量账本按现有流程更新。

为避免混淆，真实设计采用三组：

| 组 | 数量目标 | 区域策略 | 目的 |
| --- | --- | --- | --- |
| disabled | 关 | preserve | 与原二进制核对关闭功能复现 |
| release-control | 关 | release-single | 隔离释放区域偏好的影响 |
| quantity-100 | 开，lambda=100 | release-single | 与相同区域策略比较数量优化 |

阶段日志记录 `begin / after_timing / after_qp1 / after_qp2 / after_spreading / after_clip / after_legalization` 的 S、WASLL、逐接缝计数、越界网络、PU 跨 SLR 移动次数以及区域 X/Y 投影次数。扩散内部调用的区域投影计入 `after_spreading` 区间；`after_clip` 只表示后续显式投影。这两个统计不能混用。

PU 跨 SLR 移动数按 anchor Y 判断；S 则按完整 pin 坐标计算。累计阶段 ΔS 会重复计入不同迭代对同一网络的改变，不能解释成“消除了多少个唯一网络”或最终改善百分比。

## 5. 验证与交付口径

独立内核已有 86 项检查通过：所有 15 种非空四 SLR 占用掩码、扇出共享、宏偏移/同 PU 多 pin、固定 PU、优化过滤、指数数值稳定性、串并行归约、QP 符号和零权重，以及合成跨界闭环。

- 中心有限差分步长 `1e-4`，最大绝对误差约 `1.15249e-11`，要求 `<1e-8`。
- 多网络共享 PU 的串行/8 线程梯度最大误差约 `5.77316e-15`，要求 `<1e-10`。
- 合成固定 sink 算例：S 从 1 到 0，移动 source Y 从 95 到约 130.577，sink 保持 150。
- AddressSanitizer 和 UndefinedBehaviorSanitizer 检查通过。

真实 GETRF 的最终对照结果、关闭功能坐标差异、逐阶段归因和完成标准另见同批验证报告。离散 S 还将由独立 Python 程序从冻结的原始网表和最终 cell 坐标重新计算，并与 C++ 阶段追踪相互核对。该校验器会拒绝未经处理的 PCIE pin 偏移情况。

所有新配置/参数均为显式实验开关，未替换普通 Pro 默认设置。最终 DCP 不下载到本地。
