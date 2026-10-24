# Die内优先扩散实现（2026-10-24）

状态：独立副本实现及合成验证；没有启动新的AMF布局或Vivado实验。构建与最终检查结果见交付证据。

## 先保存原SLL版本

修改扩散前，已将原SLL实现 a4e5fed0e2033e51c0e6d209b757c3e337a870b7 推送到 GitHub 分支 `codex/sll-edge-compact-snapshot-20261024`，并从远端读取引用核对提交。该分支包含动态τ及统一边界紧凑net临时退出规则。

实现分支 `codex/die-first-spreading-20261024` 位于099独立副本 `experiments/preflight/20261024-die-first-spreading/repo`，不修改原实验源码、配置、结果或当前构建链接。

## 区域与失败处理

1. 每个过密种子首先只在其当前物理die内扩大矩形区域。X方向和die内Y方向继续使用原来的方向选择与扩散策略；跨边界方向不参与这一阶段的容量比较。
2. 每个区域最多200个bin，零容量bin也计数。整行/整列扩展若会超过200则拒绝，因此可能停在少于200，例如196个bin。200不是累计查询次数。
3. 若die内受限搜索仍不能满足目标容量，丢弃这一候选的临时覆盖标记，重新从原种子建立允许跨die的区域。这个新矩形同样最多200个bin，不在第一个200-bin区域之外再追加200。
4. 若汇总容量足够但写回后仍有过密，下一次原有内部迭代先做更宽的die内搜索，并实际写回一次。若该有界尝试仍过密，后续内部迭代才允许跨die搜索。固定对象、宏形状、原位置混合、合法化及区域裁剪造成的剩余过密都可能走这一路径。
5. 跨die搜索是开放候选方向，不强制迁移；仍可能选到die内区域。搜索不成功时保留实际剩余过密，继续原循环，不增大区域或增加原有最多10次内部迭代。

日志区分物理总量不足、目标有效供给不足和局部受限搜索/写回失败。后两者不证明整个die不存在合法布局；当前扩散器也不是完整的宏site匹配求解器。未新增全die可行性求解。

## 边界与完整PU写回

扩散使用私有密度视图：只有穿过物理SLR边界的网格行被切开，site容量按真实坐标分配，cell需求继承原占用值。边界精确点归下方die，与现有SLL几何约定一致。切出的片段各算一个bin；同一原bin的两片被纳入跨die区域时算两个，因此是保守预算。QP原网格不修改，未切割的行保留原高度运算。

单纯限制区域还不够：原位置混合和宏成员偏移可能使实际PU写回跨界。因此die内阶段在原写回后检查整个PU，按所有真实/虚拟成员原die归属求共同Y范围；必要时作边界投影并复核原合法化与区域裁剪。若约束冲突或浮点舍入仍使成员跨界，保留该PU本次进入扩散前的位置。同一PU被die内与跨die区域同时涉及时，die内保护优先。已有跨die宏保留各成员原归属，不强行把整个宏塞进一个die。

这一约束仅作用于本次die内扩散写回；不产生永久die绑定，不改QP步长、SLL项、原混合系数或迭代预算。没有物理SLR边界的器件继续旧扩散路径。

## 诊断

在既有 `SllReportDirectory` 下追加三份轻量日志；无该目录配置时写当前工作目录：

- `spreading-regions.tsv`：资源类型、调用/迭代号、种子、die、阶段、原因、die内候选大小与供需、die总供需、最终矩形bin数与索引范围、写回后的剩余过密、PU数、实际最大/平均位移、最大|ΔY|及跨界cell数。
- `spreading-passes.tsv`：每次内部迭代的剩余过密bin数、剩余需求、区域数和允许跨die搜索的区域数。
- `spreading-crossings.tsv`：实际跨界PU完整名称与ID、前后XY、位移和真实成员跨界数量。

剩余需求口径为Σmax(0, occupation − capacityShrinkRatio × nominalCapacity)，与原过密判断对齐；搜索使用当前有效容量。位移以本次原写回、合法化、边界保护后的实际PU anchor为准，不把候选坐标当成真实移动。同一宏若涉及多个区域，其位移可能出现在多条区域记录中，不把逐区域计数直接累加成独立PU数。实际跨界PU单独逐次记录。

原因字段：`local_capacity_satisfied`、`wider_local_writeback_trial`、`die_resource_capacity_shortage`、`die_target_supply_shortage`、`local_capacity_search_exhausted`、`local_writeback_overflow_after_bounded_search`。容量不足也必须先执行die内阶段；不会凭全die计数提前跨界。

## 参数保持与验证范围

不改变 SllQuantity、SllAdaptivePolicy、SllRunDiagnostics、WirelengthOptimizer、GlobalPlacer 及实验配置文件。γ=0.02、c=24、动态τ、统一边界紧凑net退出规则、完整S/F统计、纯一阶注入和原步长保持。SllRegionPolicy仍为preserve；release-single保留为已实现、暂不启用策略。

`checkDieFirstSpreading` 调用生产区域选择方法，覆盖智能/简单方向选择、多随机种子、容量不足、区域预算、局部写回失败状态、边界切片容量/占用守恒及宏偏移约束。`checkDieFirstSpreadingIntegration` 用四个合成LUT调用真实扩散、递归分区和PU写回，分别检查单cell及宏、固定对象、残余过密和原循环上限。既有200-bin、位移和SLL策略/集成测试一并回归。

复现入口：`python3 scripts/diagnostics/check_die_first_spreading.py --build-dir <完成的build目录> --fixture <四LUT合成配置> --output <全新证据目录>`。只执行测试辅助程序，不启动AMF flow，不触发实验队列。

未测真实GETRF布局质量、运行开销或SLR1/2需求变化。私有视图构造、局部失败后的二阶段搜索及新增日志会产生开销；效果与开销需以后另获授权的真实实验衡量。当前不作SLL/HPWL改善承诺。
