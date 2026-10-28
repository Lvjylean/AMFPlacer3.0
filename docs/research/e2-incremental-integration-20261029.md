# E2 与增量编译集成分支：实现与验证

日期：2026-10-29。分支：`codex/e2-incremental-integrated-20261029`。

## 位置与来源

- 工作副本：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261029-e2-incremental-merge/repo`。
- 服务器：eda072。开始时约 85 GiB 可用，验证后约 81 GiB；未切换 099。
- E2 来源：`20261028-e2-sll-output-tail-232754/repo`，原 HEAD `64a4660c8fd628af6864a8594a90f3773cee7d86`，包含该副本的未提交最新功能。
- 增量来源：`20261014-getrf-static-containment/repo/experiments/preflight/20261020-device-placement-policy-83/repo`，原 HEAD `39464f8f9089821e52ee80107c0ddaf18215fbf7`，包含该副本的未提交更新。
- 采用独立 Git 副本；原副本未切换分支、未提交、未覆盖。开始和结束比较原 E2 的 3931 个文件与原增量副本的 3968 个文件，内容哈希均未改变，HEAD、分支和工作区状态一致。
- 合并输入、原状态及最终保护核验记录保存在上一级目录的 `merge-inputs.json`、`original-state.json`、`original-verification.json`。
- 大型输入和 DCP 沿用服务器文件，不纳入 Git；没有下载 DCP。

已恢复增量分支的测试、配置、文档与设备策略，并保留 E2 最新优化，不能只用上述两个原 HEAD 代替本次输入快照。

## 合并后的功能

| 功能 | 集成状态 |
|---|---|
| SLL 数量优化、动态 τ、die 内优先扩散 | 保留 |
| SLL 入口/出口虚拟 cell、分段共享超边、联合 QP | 保留 |
| 跨 die 需求动态创建/撤销，需求消失后退出 QP | 回归通过 |
| 初始化：两侧线长及容量、X 靠近发送端、Y 同分处理 | 保留并验证 |
| QP 后位移上限 10，再投影到合法列，不限制投影后的换列距离 | 11、12 间距换列样例通过 |
| tile 与列容量、禁止同边界扇出拆分 | 保留并验证 |
| 当前规划 crossing 的分段 STA、工艺 SLL 延迟、出口侧长距离修正 | 保留并验证 |
| 按接收端分组的树形 FIXED_ROUTE、Vivado 选择普通出口、并行生成 | 保留，真实 Vivado 联合样例通过 |
| 原增量静态缓存、局部引脚访问、FF 修复、时钟保留、静态审计 | 源码合并与回归通过 |
| U250/VCU108 设备策略及 clock-region 列处理 | 恢复设备策略 83 并通过策略矩阵测试 |
| 全量普通布局、全量 E2、增量 E2 的独立配置路径 | 保留；普通配置不会被静默改成 E2 |

### 增量模式中新增加的连接规则

1. 在共同静态 DCP 中扫描真实已占用 SLL 节点和各 RP 的实际 route footprint。
2. 将双向视图和共享物理入口的通道归为不可分割资源组；排除静态占用，要求整个跨越段位于允许的 route footprint 内。
3. 为并行活动模块分配互不重叠的资源组。重叠 footprint 采用确定性的均衡分配；这是一种保守资源划分，并非全局最优分配。
4. 每个 worker 带入经哈希校验的保留资源表。只有发送端与全部接收端均在该活动 RP 内的 net 才参与新 SLL 规划；静态 net 和接口 net 不由新计划改写。
5. 导入前再次检查 Vivado 真实 net 的全部叶级端点、顶层端口、SLL 占用和 footprint。不能只相信缩减后的 AMF 网表或计划中的接收端列表。
6. 共同导入各模块及 SLL 计划，然后一次联合 route_design；之后审计每个接收端的实际跨越序列和整设计 SLL 使用情况。
7. 不清除既有路由包含约束。E2 增量模式下不接受与当前资源划分无关的旧 completed-module 计划。

仅能在已验证的 `internal-active-nets` 范围启用增量 SLL 规划。缺少保留表、哈希不符、计划触及接口或出现冲突时明确失败，不默默退回无约束布线。

### 时序与容量的范围

已规划的内部连接按当前虚拟 crossing 分段估计：入口侧原模型 + 工艺 SLL 延迟 + 出口侧原模型。出口侧最后一段沿用此前校准：

`t_out_corrected = t_out + 1.1430759607609453 × max(0, t_out − 1.5 ns)`

未纳入规划的静态/接口连接保留原增量延迟模型，不在活动模块的有限 SLL 资源池中独立搜索另一组 crossing。这里没有宣称静态接口延迟已经被真实布线延迟完全校准。

列容量优先目标 75%，必要时逐步放宽到 85%，tile 密度目标 80%。**在增量模式中，这些目标作用于扣除不可用资源后分配给该模块的供给，不等于“静态 + 接口 + 新内部连接”的整列最终利用率上限。** 最终整设计使用量另行审计。接口布线的新增占用也不应被误认为全部来自 E2 内部计划。

## 配置与入口

通用 E2 增量模板：`configs/placement/bases/e2-incremental-u250.json`。它保留设备与模型参数，移除了特定设计的输入网表、时钟与跟踪列表；设计和时钟约束由具体任务提供。重要设置：

| 参数 | 当前设置 / 说明 |
|---|---|
| ProPrimitiveTiming | false |
| IncrementalCompilation | true |
| SllPlanningEnable / SllJointPlacementEnable | true |
| SllPlanningAllowSplit | false |
| SllInitializationPolicy | local-wire-capacity |
| SllProjectionPolicy | qp-nearest-capacity |
| SllQpDisplacementPolicy | shared-pre-projection |
| SllColumnTargetUtilization / SllColumnMaxUtilization | 0.75 / 0.85 |
| SllDensityTargetUtilization | 0.8 |
| SllConstraintWorkers | 4，具体任务可调整 |
| SllOutputLongDelayCorrection | true |
| SllRegionPolicy | preserve；release-single 为已实现的实验策略，当前暂不启用 |
| SllIncrementalScope / SllReservedResourceFile | 由增量调度器生成并验证，不能省略资源契约手工绕过 |

`SllRegionPolicy` 控制布局阶段区域偏好，独立于 SLL 数量目标。`release-single` 删除符合条件的单体 PU 的整条区域偏好，同时取消区域锚点和越界裁侧。

推荐增量入口是 `scripts/run_parallel_incremental.py --config <任务JSON> --output <新的实验目录>`。在原任务 JSON 中将 `amf_binary` 指向本分支的构建，将 `amf_config` 指向本分支 E2 模板或其设计实例；保留实际 RTL、共同静态 DCP、模块划分和用户时钟参数。禁止覆盖历史运行目录。直接调用原生 worker 时也必须提供有效的 SLL 资源契约。

全量 DFX 入口 `scripts/run_dfx_full.py` 已接入计划导入与审计，但本次没有重跑完整 GETRF 全量 DFX 流程。普通全量及已有 E2 全量配置仍独立可用。072 启动器与脚本哈希清单已指向新副本；099 未部署新二进制，能力检查会阻止误用旧版本。

## 构建和回归证据

本次最终 C++ 构建：

- 构建源提交：`907ccdfd5cd44a85f475398ed3cea18ad6c87bd2`。
- 构建目录：`builds/build-20261029-014611-988587-907ccdfd`；`builds/current` 指向其 build 子目录。
- AMFPlacer SHA-256：`f3091500e2178da4ea554abd6a86db4a267b0889022813c851b4c75b6ca1282b`。
- 最终分支后续提交包含 Python/Tcl 接线、验证脚本、配置和说明；已核对 C++ 源文件与上述构建快照一致。
- 构建入口：`AMF_STUDY_BOOST_LIBRARYDIR=/usr/lib/x86_64-linux-gnu python3 scripts/amf3.py build --jobs 8`。

最终回归目录：`experiments/evidence/final-regression-20261029-r2`。

- 115 个 Python 测试通过。
- 24 组 C++／QP 验证通过；加上 Python 文件和编译检查共 47 个阶段，全部退出码 0。
- 这些组包括 SLL 数量/动态 τ/容量/不拆分、QP 列切换、缓存与时序等价、长距离修正、静态缓存/局部资源/FF 引脚修复、DFX floorplan、设备策略，以及实际全量和增量 QP 测试。
- 增量 QP 测试含内部高扇出连接、两条接口连接和静态 cell；内部需求消失后虚拟 cell 数量归零，静态位置保持。
- 可复现入口：`scripts/validate_integrated_source.py`。manifest 记录输入哈希、命令、阶段耗时及退出码。

## 真实 Vivado 联合验证

工具：Vivado 2026.1；器件：U250；时钟：10 ns。样例为两个跨 SLR 的 RP，每个活动模块有发送 FF、本地接收 FF、远端接收 FF，另有静态连接。实际 AMF QP 运行后生成计划；真实 cell 坐标固定，以隔离验证虚拟 crossing 和后端连接，不代表完整大设计放置质量测试。

成功运行：`experiments/runs/e2-incremental-joint-20261029-r3`。
基准样例：`experiments/runs/e2-incremental-small-20261029-v5`。

| 指标 | 结果 |
|---|---:|
| 需布线 / 已完整布线 net | 14 / 14 |
| 布线错误 | 0 |
| 计划内部 SLL | 2，各活动 RP 一根 |
| 接收端路径审计 | 4 / 4 一致，含本地端不跨 die |
| 保留的原静态 cell 位置 | 22 / 22 |
| 保留的原静态 PIP | 638 / 638 |
| pr_verify -full_check | 通过 |
| DRC Error | 0 |
| DRC Critical Warning | 1，NSTD-1：样例未指定板级 I/O 标准 |
| 最终全设计 SLL 数 | 8，含静态及接口连接 |
| WNS / WHS | −1.969 ns / +0.045 ns |
| 本轮验证总墙钟 | 235.931 s |
| AMF QP worker A / B | 8.754 s / 8.634 s |
| 联合后端（含导入、并行生成、布线、审计和 PR 验证） | 185.924 s |
| Vivado route_design 墙钟 | 约 39 s，以原始日志秒级精度为准 |

本轮验证没有执行 place_design；基准样例建立时执行过，其时间保存在基准记录中。上述 235.931 s 不含基准生成时间，不是生产设计全流程总墙钟，也不能用于宣称增量加速比。今后的全量/增量对比仍需单列 Vivado placement 兼容处理时间并保留实际总墙钟。

DCP 均在服务器：成功目录下的 `imported_sll.dcp`、`routed.dcp`；约束与审计在 `sll/`，指标在 `acceptance.json`。计划采用的精确 SLL 与实际接收端序列一致。启动时静态快照可见 4 根 SLL；最终 8 根包含模块接口恢复后可见的连接，不能将差值简单解释为规划新增量。

初期失败样例与日志保留：错误的 clock-region 查询导致没有真正跨 die；从布线前模块 DCP 恢复导致分区引脚路由丢失；未布线 net 的空 PIP 集合未作保护。这些问题已修正，失败轮次不计入通过结果。正确样例从已布线基准保留接口/时钟段，只拆除活动模块内部待规划 net。可复现生成脚本为 `scripts/diagnostics/prepare_e2_incremental_fixture.tcl`，RTL 为 `tests/fixtures/e2_incremental.v`，联合验证入口为 `scripts/diagnostics/validate_e2_incremental_flow.py`。

## 验证边界

已验证合并编译、算法回归、增量资源隔离，以及真实 Vivado 的 SLL 导入、联合布线和 PR 一致性。尚未在此新分支重跑 GETRF 大规模端到端实验、095 实机流程或生成 bitstream；本样例 10 ns 未达到 setup 时序，不能作为频率提升结果。历史增量 GETRF 成功记录保留为历史证据，不计入本分支新验证。

新分支的源代码与轻量配置完整，但器件数据库、工具、许可和 benchmark DCP 仍是服务器外部依赖，已通过配置与哈希记录，不放入 Git。
