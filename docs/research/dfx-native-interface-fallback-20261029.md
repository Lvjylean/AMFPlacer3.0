# DFX 短期可用方案：接口采用 Vivado 原生布线

用户当前要求：增量编译暂不作为主要性能优化方向，以稳定可用为先。本副本不继续接入复杂的 Partition Pin 静态树搜索。

## 当前行为

- 开启 E2 SLL 约束时，全量 DFX 后端先识别真实 RP 边界连接，将这些接口从精确 SLL 约束列表中移除，交给 Vivado 原生路由。
- 纯静态 net 和单个 RP 内部 net 仍使用已有并行导出器、接收端分组约束及逐接收端路径审计。普通非 DFX E2 后端不变。
- 原有 SLL 数量、虚拟 cell、容量和时序修正的布局计算不变；回退接口的实际 SLL 可以偏离布局计划。因此其规划时序、热点预测仍是估计，不能声称实际列容量满足规划阈值。
- `SllRouteConstraintEnable=false` 时不启用这套约束处理。没有新增需要用户设置的策略开关；本副本的 DFX 全量接口固定使用短期兼容行为。
- 增量阶段仍只为活动 RP 的内部 net 应用新约束，保留既有静态壳冻结、资源预留、路由/DRC 和接口检查。资源预留从 `common_static_locked.dcp` 提取实际占用节点，因此原生路由选中的 SLL 会被纳入预留，不能沿用未落实的原计划冒充实际占用。

## 不把“回退成功”记成“计划落实”

新增输出位于全量运行目录 `sll/`：

| 文件 | 含义 |
|---|---|
| `full-internal-plan.tcl` | 过滤后的精确约束计划；原始 placement 计划保留 |
| `full-constraint-scope.tsv` | 原计划 net 数、精确约束 net 数、原生接口数 |
| `native-interfaces.tcl` | 恢复运行时需要的接口审计记录 |
| `native-interface-route-audit.tsv` | 接口是否 ROUTED、有无 STUB，以及计划/实际 SLL 集合差异 |

`sll-audit-status.tsv` 新增 `constraint_scope`、`native_interface_nets`、`all_planned_positions_enforced`。存在回退接口时最后一项为 `0`，即使路由验收通过，也不表示全部规划位置被遵守。

原生接口允许 SLL 位置变化，但仍拒绝未布通、冲突或 STUB。内部 net 继续严格匹配原计划。缺少新接口审计记录的恢复运行会报错；旧运行仍按旧的完整精确约束记录审计，不会悄悄跳过旧 STUB。

如果在全量布线前的输入里已有试验性接口 FIXED_ROUTE，则只清除这些接口约束，批量取消其旧布线。该操作不在冻结静态 shell 的增量应用阶段执行。

## 验证范围

已完成代码级测试：新增7项（接口分类、仅本接口清理、审计、恢复、空计划、关闭开关、空 DRS 表示），原增量资源与作用域14项，均通过。

真实 GETRF/U250 全计划分类：24,795 条 SLL 计划中，171 条 DFX 接口采用原生路由，24,624 条保留精确约束；分类实测17.297秒。这是当前两 RP 设计的统计，不是所有 DFX 设计的固定比例。

GETRF/U250/12 ns 四网验证通过（Vivado 2026.1，退出码0）：

| 项目 | 结果 |
|---|---|
| 三条原有问题接口 | 全部 ROUTED、STUB=0；实际 SLL 位置均与原计划不同，明确记为原生回退 |
| 一条普通静态跨 die net | 原并行导出器生成并导入精确约束，逐接收端 SLL 审计通过 |
| 525 个端点相关 cell | LOC/BEL 未变 |
| 时钟周期 | 未变，沿用12 ns输入 |
| 恢复运行 | 成功恢复1条精确约束记录与3条原生接口记录 |
| 实际 SLL 占用 | 4条 net 合计5个 SLL 节点 |

第二轮约束生成/导入为280.384秒，包含原有检查点写出和 worker 重新打开整个 GETRF 设计的开销；第三轮复用该产物后，四网局部路由为108.380秒，审计为15.855秒。这些是诊断阶段耗时，不是新全量/增量性能对照。所有失败或中止尝试的原始时间和退出状态均保留。

此处使用局部 `route_design -nets` 验证兼容性，不作为完整时序 QoR 结论。没有启动新的 GETRF 全量布局/布线或双模块完整增量轮次；完整静态保持仍由原流程的冻结、资源占用与接口检查验收。

首轮诊断在挑选控制 net 时逐项查询 REF_NAME，筛选过慢而中止；保留中止记录。第二轮改为从原计划中按驱动引脚直接选择控制样本，不改变正式实现。该轮约束生成成功，但诊断将 Vivado 的空 DRS 字符串 `{}` 误判为残留约束，布线前停止；第三轮复用第二轮已生成的 DCP/XDC/审计记录继续验证，避免重复生成。空 DRS 识别已加入正式兼容处理和单元测试。Mac 本地 Tcl 8.5 不满足测试要求，最终测试在服务器 Tcl 8.6 执行。

## 代码位置

- 独立分支：`codex/dfx-native-interface-fallback-20261029`
- 服务器：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261029-dfx-native-interface-fallback/repo`
- 来源提交：`2530a30c3f6577be21bc1e84f878cc42dff88ea1`。
- 主要代码：`scripts/dfx_sll_native.tcl`、`scripts/dfx_sll.tcl`、`scripts/dfx_full_backend.tcl`。
- 本次仅修改 Tcl 后端及测试，不重建 AMF C++ 二进制。原分支、历史实验和 DCP 不改写；新增中间 DCP 仅保留服务器。

最终验证证据：实验根目录 `probe-r3/validation/`；全部来源、输入哈希、测试与源码提交保存在实验根目录 `manifest.json`。本轮于2026-10-30完成，目录沿用启动日期。
