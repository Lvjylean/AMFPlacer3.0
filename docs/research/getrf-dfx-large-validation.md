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

## 大规模接入补齐
- Vivado 的 IS_PRIMITIVE 会包括 DSP48E2 内部 INTERNAL 结构。导入和位置审计以完整 DSP 等可布局 primitive 为边界，不能给其内部 DSP_OUTPUT 等单独分配 site。
- 网表解析遍历层次别名后保留这些 primitive 的外部 pin，避免使用 -leaf 深入 DSP 内部。等值 GND/VCC 的多个层次驱动归一为同种常量；普通多驱动仍拒绝。
- DSP 输出寄存器 metadata 由当前 linked DCP 提取，仅认证 PREG=1 且实际使用输出全为 P[0..47] 的 DSP，沿用普通 GETRF 保守规则，不把所有 DSP 当寄存器。
- 同一不可变 linked DCP 内缓存 net 驱动查询，避免每个高 fanout sink 重复展开全部层次别名。小型 DFX 的网表、静态位置、允许/占用 site、时钟和 SRL 控制信息须逐字节回归一致。
- 3,420/3,418 是最初 Vivado 全 primitive 清单大小，含常量和内部 DSP 结构，不是 AMF 的可移动实例数；实际活动实例数以 canonical 导出和 opt 后结果为准。
