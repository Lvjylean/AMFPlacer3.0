# R10 最差路径首段绕行诊断

日期：2026-09-29。对现有 routed DCP 进行只读查询，没有重布局、重布线或写回 DCP。

结论更新（2026-09-29，依据用户后续查询）：本例绕行归因于 fanout 分布过于零散，driver 的放置需要兼顾各个负载，共享布线树因此拉长；不将布线资源紧缺列为这段绕行的原因。撤回此前“高扇出布线树拓扑与局部拥塞共同作用”的归因：源端落在拥塞窗口内只是空间关联，不足以证明该支路由拥塞造成。

当前工作范围：用户明确将高扇出优化留待后续处理；GETRF 暂不作为当前优化工作的主要效果评测 case，尤其不以这条 WNS 路径评价边界、延迟或扩散优化是否有效。已有完整流程验证、历史指标和 DCP 保留。本次仅修正诊断与评测范围，不启动高扇出优化或新实验。

## 输入及证据

- R10：`experiments/runs/getrf-u250-full-20260929-133345-334380`
- DCP：服务器上述目录下 `reports/getrf_routed.dcp`
- SHA256：`fa1973d8766aaebfd12d3381d78cacd2aaee94bf46cace793c1fbe54c8698d49`
- Vivado 2024.2，诊断进程最多 4 线程。
- 服务器诊断：`experiments/preflight/20260929-r10-critical-route-160757/`
- 本地轻量证据：`experiments/evidence/20260929-r10-critical-route/`

源：`grp_getrf_core_double_2_512_256_s_fu_1395/s_reg_11314_reg[0]_rep__0/Q`

目标：`grp_getrf_core_double_2_512_256_s_fu_1395/grp_subUpdate_double_2_512_s_fu_6074/mul_9ns_2ns_11_1_1_U1576/dout[0]_INST_0_i_14/I3`

## 核实结果

| 项目 | 数值 |
| --- | --- |
| 源 site / tile / CR | SLICE_X98Y183 / CLEL_R_X61Y183 / X3Y3 |
| 目标 site / tile / CR | SLICE_X144Y211 / CLEL_R_X91Y211 / X4Y3 |
| 关键支路经过的 SLR | 仅 SLR0 |
| 整条时序路径 SLR / IO crossings | 0 / 1 |
| 此 net 扇出 | 256 |
| 负载分布 | 30 个 CR；SLR0 有 83 个，SLR1 有 173 个 |
| 最差支路延迟 | timing_summary 9.396 ns；NET_DELAY.SLOW_MAX 9.395 ns |
| 整条数据路径延迟 | 9.946 ns |
| 整条路径逻辑 / 布线延迟 | 0.490 / 9.456 ns |
| WNS | +0.036 ns |
| Vivado Estimated Path Delay | 2.102 ns |
| Vivado Estimated Net Delay | 1.612 ns |
| Hold Fix Detour | 0 |
| 经过此首段的最差 hold slack | +5.001 ns，终点为 mul_ln44_reg_101_reg[1]/D |
| 固定 LOC / 固定 route | 5 / 0 |
| 关键支路 / 全 net 使用的 PIP 数 | 143 / 1814 |
| 关键支路 PIP 的 tile 坐标包围盒 | X=2..93，Y=100..233 |

9.396 ns 占整条数据路径约 94.47%。143 个 PIP 是从该 net 的已用 PIP 连通图中重建的源到指定 sink 的支路，不是全部 256 个负载的布线树。tile 索引并非等长物理坐标，不把索引距离比称为物理线长比。

## 历史拥塞观测与归因更正

`congestion.rpt` 的 Router Initial Congestion 显示南向 Long、level 5 窗口：
`CLEM_X59Y135 -> CLEL_R_X74Y198`。
源 tile `CLEL_R_X61Y183` 位于其中。该窗口 LUT/FF 占用分别约 43%/18%，这些是逻辑资源占用率，不是布线资源可用率。

原路由日志也有 level 5 (32x32) 时序拥塞提示。该统计反映路由早期的局部需求压力，不能解释成最终所有通道均已用尽。最终 route status 仍为全部布通、零冲突。

256 个负载跨 30 个 CR 和两个 SLR；路由器为整个网络构造共享布线树，指定 sink 的支路不必是两端几何最短线。用户后续查询将本例定位为负载分散引起的放置与共享树问题。上述拥塞窗口及 IO 跨列作为原始观测保留，不再据此推断它们造成了本次绕行。

Hold Fix Detour=0 表示未报告显式 hold 修复绕行；AMD 说明该标记只覆盖部分修复阶段，不能把 0 当成绝对排除所有 hold 影响的证明。本例 +5.001 ns 的最小路径余量也不支持“为满足当前目标支路 hold 而必须增加数 ns”这一解释。

这条关键支路不跨 SLR。当前 AMF 的几何距离与固定边界惩罚未表示具体通道占用和实际高扇出路由树，单独增大 SLR 惩罚不能直接修复此类问题。

参考：

- [AMD UG906：Physical path analysis / Net Fanout and Detour](https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/Category-3-Physical)
- [AMD UG949：High-fanout nets in congested areas](https://docs.amd.com/r/en-US/ug949-vivado-design-methodology/Limit-High-Fanout-Nets-in-Congested-Areas)

## 可复现诊断与限制

`scripts/diagnostics/inspect_r10_critical_route.tcl` 导出端点、负载、PIP、node、拥塞和时序；`report_r10_critical_route.tcl` 导出 routed-vs-estimated 和 hold detour 特征。
初次诊断有两项报告参数不兼容，错误和原脚本快照保留；随后补充诊断成功，主诊断脚本已修正参数。布局布线结果未改变。

`route_geometry.json` 及 `critical_branch.tsv` 使用有向 PIP 连通图重建。源/目标节点由实际 site pin 查询获得；只有 IS_DIRECTIONAL=0 的 PIP 才加入反向边。

未进行拆除其他 nets 后重布线等反事实试验，所以不声称已证明唯一原因或最短可实现路径。Vivado 的 estimated delay 是其布线前代理，不是 AMF 模型输出；原 R10 自动生成的 `unique_timing_samples.tsv` 中 delay-model 字段仍使用旧系数，本诊断未将它作为 R10 新拟合模型的估计值。
