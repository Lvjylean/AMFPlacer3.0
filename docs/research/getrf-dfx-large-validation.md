# GETRF 大规模双区域 DFX 验证（2026-10-13）

## 目标与冻结条件
- 用户交付 GETRF：NRC=512、NCU=256、双精度、带主元。交付包 wrapper 与 39 个相关库文件均与 Vitis 2026.1 缓存哈希匹配。
- 使用该次 HLS RTL，HLS 排程周期为原有 5 ns；本轮 AMF 与 Vivado 物理实现约束均为 **10 ns**，没有重新运行 HLS 或缩小参数。
- 只在独立副本 experiments/preflight/20261013-getrf-dfx-large/repo 修改。
- AMF：普通 Pro，ProPrimitiveTiming=false，12 线程，网格 batched，legacy_components；沿用 DFX 静态区固定、Pblock site 允许列表、静态布线保持机制。
- 新增 DFX Pblock、RP 边界及测试封装，不能直接与历史 GETRF 实验归因比较。

## 选定区域

| ID | 原层次实例（core 内） | Pblock | SLR |
|---|---|---|---|
| a | grp_subUpdate_double_2_512_s_fu_5268（CU0） | CLOCKREGION_X2Y1 | SLR0 |
| b | grp_subUpdate_double_2_512_s_fu_6548（CU128） | CLOCKREGION_X2Y5 | SLR1 |

层次前缀：kernel/grp_getrf_core_double_2_512_256_s_fu_1411/。
两个 RP 分别在不同 die，各自不跨 die 接缝。每个初始 Pblock 一个 CR；以 Vivado snapping 后实际范围为准。
只读器件查询：每个 CR 含 1,320 SLICEL、960 SLICEM、120 DSP48E2，及 36 个 RAMB36 等资源。
这是大规模静态上下文中的两个较小但真实的计算模块；速度比不泛化为任意大修改比例。

## 真实变更与等价性
- V0：原控制器；V1：只将两状态外层 FSM 从 2 位 one-hot 改为 1 位 binary。
- 保留端口、握手、数据宽度、流水线时延和浮点实现；其余 254 个同类模块不修改。
- V0/V1 生成树只允许 getrf_dfx_rm.v 内容不同；实例标记与封装在两版相同。
- 实际原/新 RTL 控制器完成 131,072 个穷举转移检查，包括可达 FSM 状态、reset/start/child done/ready/启动寄存器和 rs/cs 全值域；逐周期比较父输出和所有算术子模块输入。非法 one-hot 状态不在等价域内；初始化、复位和合法转移保持可达域。
- 此为控制器等价证据，不声称已在 FPGA 上执行矩阵数值回归。算术子模块逐字节不变。
- OOC 综合：V0 3,420 个 primitive，V1 3,418 个；接口签名相同、primitive 清单不同。OOC 检查不是完整 DFX 验收。

## 三个比较对象
1. V0 静态基线：完整综合、Vivado 布局布线，生成基线 full/partial bitstream、每个 RP 的 full-context locked shell。
2. V1 全量参考：相同新版本、同样 Pblock/工具/约束，重新完整综合和 Vivado 布局布线，不复用 V0 路由。
3. V1 AMF 增量：复用 V0 静态布局和布线；两个独立进程从 V1 RTL 各自综合 RM，再执行 AMF 和 Vivado 后端，最后组装两个 routed RM 并验证。两模块本轮实现相同，但仍分别执行综合并保留时间，以验证并行前端路径；不把预检 DCP 缓存复用当作完整增量成本。

**全量参考的 placer 是 Vivado，增量路径的 placer 是 AMF。** 比较的是“全量 Vivado 实现”与“AMF 增量流程”的实际成本，不是同一个 AMF placer 全量/增量内核的纯加速比。不能用历史另一种 GETRF 输入的 AMF 时间替代本轮全量时间。

使用无普通封装引脚的 JTAG 输入/输出扫描封装，保留 HLS 内核可控性与可观测性，避免随意分配 OOC AXI 端口为板级管脚。三者封装完全相同。此为实现/DFX/bitstream 验证，不是 DDR/PCIe 平台或板上 GETRF 运行测试。

## 正确性验收
- 接口一致且网表确实改变；静态设计未因恒定输入被删除。
- 10 ns 时钟、RP 实际 SLR 和边界、导出覆盖、AMF 导入及最终位置保留率。
- 静态 LOC/BEL、路由及 partition pin 保持；每个 worker 及组合均执行 PR verify。
- 全量布线、DRC errors/critical warnings、setup/hold/pulse width 分别记录。
- 组合后的模块布线与 worker 一致，生成 full bitstream 和两个 partial bitstream。
- DCP/bitstream 只留执行服务器。

## 时间口径
保留真实起止和墙钟；单列 IP 生成、全量/RM 综合、DCP→AMF 导出、AMF 进程及细粒度 placement、转换/导入、Vivado placement、routing、审计、读写 DCP、bitstream 和组合。
增量主指标按用户要求排除 Vivado placement（兼容开销），原始时间照常记录。并行 worker 按实际时间区间和关键路径计算，不能从墙钟重复扣除重叠的 placement 时间。
V0 建壳为一次性成本，不计入一次 V1 增量复用成本，另报首次建立成本。HLS/IP 缓存复用单列。
相同 V1 的全量和增量验证完成前，不输出加速比。

## 状态
2026-10-13：源码核对、RTL 修改、控制器检查及 OOC 综合已通过；大规模完整 DFX 结果待验收。

### 2026-10-13 综合后属性检查修复

- V0 全量综合 1,792.420 秒，V1 全量综合 1,777.863 秒；分别写出 base_synth.dcp，综合均为 0 error、0 critical warning。完整设计的层次 cell 查询返回 1,022,006 项，未发生静态上下文被整体优化掉的情况。
- 原脚本对每个 cell 调用 list_property 寻找 bblock。只读重放中，前 1,000 个 cell 就需 9.928 秒；批量查询整个网表耗时 9.573 秒且准确返回两个配置 RP。这是元数据检查的实现开销，不是 AMF placement、Vivado opt/place/route 耗时。
- 修复改为批量筛选，保留 true/1/yes 的原大小写语义、完整标记集合与配置集合一致性校验，不改变 Pblock、时钟或布局算法。实际 DCP 上大小写变体检查通过；恢复入口和 DFX 合同共 11 个测试通过。
- 15:46 HKT 仅停止这两个精确识别的 Vivado 批处理 PID，原 run 和日志保留。二者尚未执行 opt/place/route。原调度器正常记录子任务失败并退出；主动中断原因另存 attribute-scan-recovery-01/interruption.json，不能将退出码解释为设计 DRC 失败。
- 恢复前核对 RTL/IP/XDC 内容哈希、器件、周期、模块、分区、Vivado 路径与线程配置，不重新综合。V1 增量队列指向恢复后的 V0 壳。
- resume-02 启动发现修复脚本的额外闭括号使 Pblock 操作在 main 外提前执行，读取 DCP 前即退出；失败现场保留。这是恢复脚本错误，不是设计问题。已修复作用域并补上 Tcl 脚本加载回归；DFX 与恢复测试共 18 项通过。
- 15:53 HKT 启动独立 resume-03，两组继续复用原 01 的综合 DCP。源码提交 01be943717a58a3572ad7eb5ed3c1b484edc2407；调度 PID 1169199。
- 原综合时间、失败检查开销、恢复读 DCP 开销和包括中断的总墙钟分开记录；不能把恢复 run 的墙钟直接称为完整编译耗时。
- 最新恢复索引：experiments/evidence/getrf-dfx-large-01/recovery-launch.json；当前 campaign 为 attribute-scan-recovery-02/campaign.json。

## 大规模接入补齐
- Vivado 的 IS_PRIMITIVE 会包括 DSP48E2 内部 INTERNAL 结构。导入和位置审计以完整 DSP 等可布局 primitive 为边界，不能给其内部 DSP_OUTPUT 等单独分配 site。
- 网表解析遍历层次别名后保留这些 primitive 的外部 pin，避免使用 -leaf 深入 DSP 内部。等值 GND/VCC 的多个层次驱动归一为同种常量；普通多驱动仍拒绝。
- DSP 输出寄存器 metadata 由当前 linked DCP 提取，仅认证 PREG=1 且实际使用输出全为 P[0..47] 的 DSP，沿用普通 GETRF 保守规则，不把所有 DSP 当寄存器。
- 同一不可变 linked DCP 内缓存 net 驱动查询，避免每个高 fanout sink 重复展开全部层次别名。小型 DFX 的网表、静态位置、允许/占用 site、时钟和 SRL 控制信息须逐字节回归一致。
- 3,420/3,418 是最初 Vivado 全 primitive 清单大小，含常量和内部 DSP 结构，不是 AMF 的可移动实例数；实际活动实例数以 canonical 导出和 opt 后结果为准。

## 全量部分正式结果（2026-10-13 17:34 HKT）

| 指标 | V0 基线 | 相同新设计 V1 的全量参考 |
|---|---:|---:|
| report_timing_summary WNS (ns) | +0.020 | +0.029 |
| TNS (ns) / setup 违例端点 | 0 / 0 | 0 / 0 |
| WHS (ns) / hold 违例端点 | +0.009 / 0 | +0.010 / 0 |
| 全布线 net 数 / 可布线 net 数 | 909529 / 909529 | 909656 / 909656 |
| DRC Error / Critical Warning | 0 / 0 | 0 / 0 |
| Vivado placement (s) | 1365.974 | 1395.839 |
| Vivado routing (s) | 3158.696 | 3050.294 |
| Rip-up 全局迭代编号 | 0–3，共 4 轮 | 0–1，共 2 轮 |
| 初始 Global/Short / Timing congestion | 5 / 5 | 5 / 6 |
| full 与两个 partial bitstream | 已生成 | 已生成 |

两组分别为 experiments/runs/getrf-dfx-v0-shell-resume-03 和 getrf-dfx-v1-full-resume-03。正式 V1 WNS 为 +0.029 ns，区别于 Router 估计的 +0.028 ns。两组 routed DCP 都已保存，DCP/bitstream 不下载。

V1 全量流程已完整退出 0。V0 仍在从同一个 routed DCP 派生两份 static shell，不把此时状态标为完整 baseline 流程已结束。模块 a 位于 SLR0，模块 b 位于 SLR1；每个 worker 只允许修改自己的模块，另一模块仍属于该 worker 的固定上下文。

2026-10-13 用户再次明确要求立即启动两组增量。现有一次性调度器在两份 shell 完整写出、baseline manifest 完成并完成产物哈希验证后自动并行启动，max_parallel=2；每组 AMF 12 线程、Vivado 4 线程、10 ns、ProPrimitiveTiming=false。不得把未写完的 DCP 交给 worker，也不重复启动另一套调度。

17:43:38 HKT：V0 baseline 完整退出 0，两份 static shell 均已写出并登记哈希；调度器已启动 experiments/runs/getrf-dfx-v1-parallel-03，runner PID 1201132。17:43:58 核实 frontend/a 与 frontend/b 两个独立 Vivado OOC 综合进程同时运行（PID 1201257、1201259），均已获得综合许可。此时是 RM 综合阶段，尚不能称 AMF placement 已开始。后续自动执行两个 AMF/Vivado worker、routed RM 组合、静态保持验证、PR verify 和 full/partial bitstream 生成。


## 2026-10-13 18:33：按用户要求跳过新增静态位置复查并续跑

用户要求跳过接入 RM 后的新增全量静态位置检查。18:26 仅向已核实的准备阶段 Vivado PID 1202408/1202483 发送 SIGTERM；旧 worker、runner、coordinator 已退出，历史产物保留。旧运行没有 linked.dcp，因此重新读取 shell、接入 RM，不重跑模块综合或 V0/V1 全量实现。

新运行：`experiments/runs/getrf-dfx-v1-parallel-skip-static-audit-04`，18:32:50 HKT 启动，supervisor PID1211430、runner PID1211432。A/B 已并行读取 shell，Vivado PID1211551/1211552。源码提交 `d110ba507e9f37b02184c59c1c4bec228290eddd`，冻结 AMF 二进制及布局参数不变；新配置 `getrf-dfx-v1-incremental-skip-static-audit-04.json` 显式设置 `static_placement_audit=skip`，默认仍为 full。

跳过各阶段重复的全静态实例类型/LOC/BEL 对比，包括导入、报告和组装中的同类检查；记录为 skipped，不冒称通过。初始占用快照仍用于静态 site 排除。接口、硬 Pblock、静态 routing lock、完整导出覆盖、RM 导入/组装保留、DRC、时序以及每 worker/最终组装的 `pr_verify -full_check` 均保留。

A/B 复用旧运行 `frontend/a/rm.dcp` 和 `frontend/b/rm.dcp`；来源、综合完成记录、RTL/IP 及 DCP 哈希均核对。23 项 DFX 回归通过，覆盖 skip 不读取/重算快照、full 检测位置改变、接口检查保留。证据 `experiments/evidence/getrf-dfx-large-01/skip-static-audit-01/`。旧检查耗时、恢复读入开销、原综合耗时分别保留；续跑墙钟不能当作从 RTL 开始的完整增量时间。


## 2026-10-13：Pblock 范围内资源表与 linked DCP 接续

用户要求删除增量输入准备中的全器件资源查询冗余。副本提交 `9df0ee51f1faf74d319ffa7caaee15b0d8e5aab2`：

- `dfx_common.tcl::resource_tables` 只访问当前 Pblock 的 site、PROHIBIT 属性和该区域内已放置的静态原语；记录 resource_scope.tsv 和三个细分计时。
- Pblock 外由 AMF 的硬允许列表排除；Pblock 内禁用及静态占用仍排除。EXCLUDE_PLACEMENT 区域若出现静态原语则失败，不假定其必为空。
- 用户已选择 static_placement_audit=skip，因此不再为了资源表制作初始全设计静态位置快照；full 模式仍保留原有静态位置审计。
- 本修改没有删去完整静态 fabric 时序上下文，没有改变 AMF 二进制、cell/net 导出语义、边界时序模型、10 ns 约束、线程数、Vivado 静态布线锁定或 PR verify。仍未实现仅 RM 加抽象边界时序的输入模式。
- 支持经父运行身份、输入哈希、写 DCP 阶段退出状态核验后从 linked.dcp 恢复；拒绝正在运行的父任务、输入改变或未完成 DCP。恢复复制后再次核对哈希与边界接口。

旧运行 `getrf-dfx-v1-parallel-skip-static-audit-04` 的两路 prepare 在资源表生成段由本次用户授权中止，退出 143；现场不覆盖，原因与精确 PID 保存在 `experiments/evidence/getrf-dfx-large-01/local-resource-01/interruption.json`。

新运行：`experiments/runs/getrf-dfx-v1-parallel-local-resources-05`，2026-10-13 19:29 HKT 启动。复用 04 两路 linked.dcp，以及 03 已完成的 RM 综合结果。继承综合/link/opt 成本、旧扫描中断成本、新 DCP 复制/读取成本分别记录，不能把恢复运行墙钟直接当作完整增量首次耗时。

验证：服务器 28 项 DFX 回归 + 6 项接口回归通过；新增固定数据回归验证局部资源法与全局扫描法的可用候选相同，仍排除 PROHIBIT/静态占用，且拒绝错误的区域/恢复来源。实际 Vivado 资源表结果与耗时等待本轮验证，不把单元测试冒称完整实现验收。

实际 Vivado 验证（2026-10-13 19:34 HKT）：两路 Pblock 均为 2520 site，allowed=2434、局部 PROHIBIT=74、静态原语=0。新旧 allowed_sites.txt 逐字节一致；资源表总耗时 A=10.185s、B=10.866s（包含属性查询 10.172/10.854s 和局部占用查询各 0.001s，不可与子计时重复求和）。两路均成功接续到后续输入导出。证据 local-resource-01/real-resource-validation.json；完整 AMF/后端仍未验收。
