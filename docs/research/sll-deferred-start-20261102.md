# sll约束优化延后

本次将已验收候选 `82b5a948`（延后启用）和 `c425bb98`（只读逐帧SLL记录）整理为一个提交，发布到 `codex/sll-edge-compact-snapshot-20261024`，保留上次“2.0优化加速版本”的全部代码和文档。源码、脚本、配置和测试树与实测候选c425bb98一致；发布时不重新运行布局布线。

## 启用规则

- `SllDeferredUntilFirstSpread` 默认 `false`；缺省或false仍使用原有初始化规则。设为true要求SLL规划和联合QP功能开启，输入校验同时检查二进制能力 `deferred_sll_start_schema=1`。
- 初始化QP和首轮全局QP暂不分配虚拟SLL，也不保留提前选出的tile。首轮全部资源扩散完成仅置pending，在下一次QP中根据当时真实cell位置创建规划。当前GETRF阶段预算下，QP1–8关闭、QP9开启；这是流程事件控制，不是硬编码第9次启用。后续阶段不会重新关闭或初始化。
- SLL数量梯度仍从第一轮全局QP开始，动态tau策略不变。提前最终合法化、在只读批次内部激活会被拒绝。
- `SllRouteConstraintEnable` 与延后开关独立。本轮为false，保留最终容量合法化和报告，不导出、生成或导入SLL位置FIXED_ROUTE，后端执行默认自由布线。
- `visualization=true` 时每个真实坐标帧配对输出只读SLL快照，包含入口/出口坐标、net/driver及容量。未启用状态与合法零crossing区分；未选物理通道时通道字段留空。记录器不刷新、不合法化，不改变规划决策，不用最终位置补历史帧。

## 已验收的本轮配置

GETRF/U250、10ns、eda072、AMF12线程、Vivado2026.1四线程、普通Pro且 `ProPrimitiveTiming=false`。

| 参数 | 本轮值 |
|---|---|
| SllDeferredUntilFirstSpread | true |
| CrossDieSpreadMaxBins | 20 |
| visualization | true |
| SllRouteConstraintEnable | false |
| SLL数量、动态tau、新L、虚拟QP、容量和时序优化 | 保持开启 |
| SllRegionPolicy | preserve |
| AllowSplit / DetailedDiagnosticsEnable | false / false |

20bin限幅是之前已有实现，本轮仅改变配置。参考点为本轮QP后、扩散前；由跨die搜索PU激活，**不是所有cell所有方向的全局位移上限**。本次没有新增连接感知方向约束。`SllRegionPolicy=release-single`仍为已实现但当前不启用的实验策略。

复用综合优化后的输入，没有重新综合。未使用DFX/Pblock、DONT_TOUCH高扇出保护、CARRY预连接、额外-delay重布或phys_opt。

## 验收范围与实测结果

小型U250四寄存器集成验证覆盖开关缺省/false/true、旧联合QP回归、禁止过早最终合法化、禁止只读批次内激活；逐帧记录覆盖出生/移动/消失和无通道合法化副作用。完整生产运行退出0；早期16帧未启用，第17帧/QP9首次记录7138 crossings；440对坐标/SLL帧及events、gzip CRC/footer/count全部通过。

| 指标 | 本轮结果 |
|---|---:|
| WNS / WHS | +0.157 / +0.019 ns |
| TNS / THS、setup / hold失败端点 | 均0 |
| 完整布通 | 921787 / 921787 nets |
| 路由错误、DRC Error / Critical Warning | 均0 |
| 原始LOC/BEL保留 | 856998 / 856998 |
| AMF进程墙钟（含可视化） | 1772.815304 s |
| 默认route_design墙钟 | 1199.117 s |
| 生产原始总墙钟 | 4538.011856 s |
| GP / QP | 37 / 92 |
| 规划 / 实际SLL | 7830 / 7830 |
| 规划 / 实际最热列占用 | 75.9028% / 90.6944% |
| 实际tile对：>80% / >90% / 100% / 超容量 | 142 / 115 / 99 / 0 |

使用导入兼容修复策略，`strict_import_verified=false`，不能表述为完全不经修复的导入；原856998 LOC/BEL全部保留。保留继承的OOC时钟源/IO约束范围及普通DSP等警告，DRC Error/Critical Warning为0不等于无任何warning。

同二进制、同可视化、20bin关闭的布局参考为1745.590364秒：本轮增加27.225秒（1.56%）。旧延后SLL、20bin关闭的无可视化布线参考WNS +0.019ns、route1535.359秒、actual SLL6937；本轮时序和布线时间改善，SLL数量和局部热点增加。布线参考源码包含不同的逐帧记录支持，且缺少同密度可视化开销，不能把所有变化归因于延后启用或20bin。

中间round5的SLR2上半区团簇仍有2632个真实cell，其中108个与下方die有数据直连；它们属于已在SLR2的die内扩散，现有限幅未全覆盖。最终20bin开/关两轮上半区均为0，因此不能把最终上部空白独自归功于20bin。WNS受具体高fanout分支和布线绕行影响，不能仅凭一次结果推断布局或Vivado seed的单一因果。

## 来源

机器可读信息和证据哈希见[provenance](../provenance/sll-deferred-start-20261102.json)。服务器冻结源码：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261101-sll-deferred-visualization/repo`

该源码下验收目录 `experiments/comparisons/getrf-u250-10ns-sll-deferred-spread20-vis-route-20261102`；运行目录 `experiments/runs/getrf-u250-10ns-sll-deferred-spread20-vis-route-20261102-full-20261102-013920-149889`。最终DCP仅服务器运行目录下 `reports/getrf_routed.dcp`，不进入Git。

本次Git提交包含生产代码、测试和轻量发布说明，不包含二进制、完整帧、实验大文件或DCP。
