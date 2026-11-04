# AMFplacer3.0 硬件资源适配版本

本分支保存为 llama-fpga U250 全设计完成的硬件资源、时序边界、打包及 Vivado 导入适配。算法与正式脚本保持已验证提交 `540d0077701991afabe4ec139df8fc5d700fa395` 的原样；此次归档只增加版本说明与来源记录。

现有结果是：AMF 布局完成，Vivado 默认 repair 导入与布局完成，随后独立布线试验全部布通，DRC Error 和 Critical Warning 均为 0；**3.332 ns 的 setup 时序未收敛**。既有 8 个预留半区占用按用户批准保留为例外，不能记成“全部半区无占用”。

## 代码来源

- 分支：`硬件资源适配版本`。
- 服务器主仓库：`/Projects/jinyang/workspace/AMFplacer3.0`。
- 独立工作区：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261104-hardware-resource-adaptation-release/repo`。
- llama 适配前基线：`0a78b00f7cdc56f4428c656256f7462491d0244d`，即此前 SA 跨 die 补偿高级版本。
- 已验证主体代码：`540d0077701991afabe4ec139df8fc5d700fa395`，包含基线后的全部 11 个提交。
- 主仓库原分支及其未提交工作保持原样，历史适配副本、构建和实验未改写。
- Git 对象导入时补齐了原浅克隆边界之外的父提交历史；新分支不依赖历史副本的 alternates 对象路径。详细记录在同目录归档证据中。

已有 U250 器件模型、跨 SLR 代价、SLL 规划、SA 补偿、SRL share4 和增量编译等能力随基线保留，不把它们重新计作本轮 llama 新增适配。

## 为 llama 完成的主体适配

| 适配范围 | 原问题与本版处理 | 主要代码入口 |
| --- | --- | --- |
| 通用全设计输入 | 从实际 case 清单和原语类型建立规范单元边界，摆脱 Minimap2 特定层次；未知或不支持类型明确拒绝。准备网表、接口、控制和属性元数据来源保持一致。 | `scripts/prepare_full_amf.py`、`prepare_full_design.tcl`、`export_full_inventory.tcl` |
| 固定接口与时钟资源 | 补齐 E4 MMCM/PLL 类型、资源映射及导出；覆盖本例 DDR4/PHY、PCIe、GT、时钟及接口 IP 的实际固定位置、site 占用、后端回放和合同检查。保留原板卡、派生时钟和 IP 约束。 | `DesignInfo.cc/.h`、`InitialPacker.cc`、`full_design_interfaces.tcl`、`full_interface_contract.tcl` |
| 复合 I/O 与双向 pad | 对复合 I/O 使用规范外层单元，排除已由该单元代表的内部 I/O，避免重复计数；双向 pad 连通性单独记录，数字侧输入、输出分别建模。478 个复合 I/O、432 个双向 pad 边界进入检查。 | `export_full_inventory.tcl`、`full_design_metadata.py`、`PlacementTimingInfo.cc` |
| I/O 时序边界 | 将 I/O 的数字发送端和接收端分别作为时序起点、终点，按适用周期估计布局延迟预算，避免把双向连接当作普通有向组合回路。这里的余量不是 Vivado 的实际 slack。 | `DesignInfo.h`、`PlacementTimingInfo.cc/.h`、`check_composite_io_timing.cc` |
| ASYNC 同步链 | 按直接连接、时钟、控制及极性形成可移动同 SLICE 组；4,171 个 ASYNC FF 形成 1,785 组。兼容组内最多 16 个 FF，跨时钟连接不强行纳入同一组。导入时先在受控事务内声明整组目标，再恢复 ASYNC_REG，避免逐单元放置引发连带移动和位置冲突。 | `InitialPacker.cc`、`run_full_backend.py`、`test_async_import_preconstraints.py` |
| URAM 专用级联 | 按真实 CAS 输出至 CAS 输入连接构造不可拆的线性 PU，保持成员偏移，连续同列同 SLR 合法化并完整导出。本例 4 条三级链、8 条级联边通过；独立 URAM 普通数据连接仍可跨 SLR。分叉、环等未支持结构明确拒绝。 | `InitialPacker.cc`、`HardResourceUtils.cc`、`PlacementInfo.cc/.h`、`ParallelCLBPacker.cc` |
| FF 控制极性及半 SLICE 兼容 | 从同一 DCP 提取完整引脚反相信息，将 CLK/SR/CE、控制类型和极性纳入兼容判断；覆盖初始打包、试插入、交换、移除及最终 BEL 检查。修正控制类型被布尔返回值压缩的问题。未支持的反相 CE/GE 明确拒绝。 | `full_ff_controls.py/.tcl`、`DesignInfo.cc/.h`、`CLBSiteLegality.h`、`ParallelCLBPacker*` |
| AND2B1L 组合传播 | 该原语使用锁存器物理资源，但逻辑上是组合门。保留其物理分类、引脚和控制极性规则，仅从时序端点分类排除，使 DI/SRI 至 O 的传播、关键路径和聚类图继续穿过它。 | `DesignInfo.h`、`check_and2b1l_timing.cc` |
| MUX 与长 SRL 结构 | 支持 fabric/FF 驱动的 MUXF7/F8 输入所需直通资源；长 SRL 链按物理可实现段划分，保留专用相邻级联及 A6LUT 的外连端点。支持本例 4 条各 128 单元的长链。 | `InitialPacker.cc`、`ParallelCLBPacker_PackingCLBSite.cc`、MUX/SRL 专项测试 |
| 区域纠正与扩散预算 | 先应用已有区域纠正，再建立本轮共享 20 bin 的扩散参考点，消除区域要求与位移预算无交集的阻塞。区域纠正位移单列；20 bin 不限制相对原 QP 点的总位移。区域选择算法本身未因此改变。 | `GeneralSpreader.cc/.h`、`GlobalPlacer.cc`、`CrossDieSpreadLimit.h` |
| CARRY 与 LUT6_2 唯一归属 | 初始打包、直通预留和最终槽位重建共用既有直接 DI 吸收条件，防止 LUT6_2 经 S/O6 入宏后再经 DI/O5 重复登记。使用常数时间判断，无新增全网扫描或缓存。 | `CarryLUTRules.h`、`InitialPacker.cc`、`ParallelCLBPacker_PackingCLBSite.cc` |
| LUT6_2 导入配对 | 清理普通 LUT 分组时跳过 LUT6_2 内部成员，保留内部配对和物理对应 BEL；增加库存、类型、配对变化和真实双 BEL 占位审计。 | `lut62_pairing.tcl`、`run_full_backend.py`、`full_backend.tcl` |
| CARRY 半区 FF 预留 | 对 LUT6_2 O5 与 CARRY 输出需要直通的冲突情形，复用虚拟 FF 预留机制。llama 新增 520 个半区、2,080 个 FF 槽位预留；专项检查中 GETRF 的预留及 LUT 归属不变。 | `InitialPacker.cc/.h`、`tests/check_carry_half_routethrough.py` |

## 验收脚本修复的归属

后续独立验证还修正了 `requirePlaced` 的布尔值传给 Tcl `%d` 的报告错误，补齐 `amf_trace` 依赖加载及入口依赖检查，并采用独立 batch 监督实际进程与退出码。这些修复在独立验收目录中，不是上述主体代码提交之后的算法改动。

本次归档没有把带本轮绝对路径、冻结清单和 8 项特定例外的验证脚本冒充通用生产入口，也没有将例外自动加入普通 `full-run`。确切脚本、输入哈希与阶段记录仍由最终运行的 `manifest.json` 及各修复目录追溯。完整重现实验需使用这些记录，而非仅凭本分支名字推断所有专项审计已集成到统一入口。

## 已验证的结果

最终独立布线路径：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261102-llama-u250-interface-adaptation/fixes/20261103-route-script-dependencies/validation/route-20261103-214618`

| 检查 | 结果 |
| --- | --- |
| 工具与 route_design 退出 | 均为 0，route_design 实际调用一次 |
| 布线 | 1,062,896 条可布线网络全部布通；剩余及布线错误网络均为 0 |
| DRC | Error 0，Critical Warning 0；另有 Warning 725、Advisory 88，原严重性保留 |
| Setup | WNS −6.207 ns、TNS −111,761.648 ns，140,486 个违例端点；未达标 |
| Hold 与 pulse | WHS +0.001 ns，hold 违例 0；pulse 违例 0 |
| 时序范围 | 无未约束内部端点和无时钟引脚；仍有 2 个输入、4 个输出端口缺外部 delay |
| AMF 导出覆盖 | 请求 1,156,542 个单元，全部存在且放置；不是整个 Vivado 原语库存的同义计数 |
| 位置保留 | 相对 AMF 的 LOC/BEL 精确保留 99.38195%，site 保留 99.99723%；分别有 7,148 和 32 个变化 |
| 后端补放 | 本轮动态 pending 27 个，均真实占 BEL、类型不变 |
| ASYNC | 4,171 个均占位且保留 ASYNC_REG；1,785 组均同 SLICE |
| LUT6_2 | 2,236 对均形成实际物理配对，配对变化和成员类型错误均为 0 |
| 预留半区 | 共 2,744 个，含新增 520 个；缺失 CARRY 0、新增冲突 0；既有 8 项占用仍在，按用户授权暂缓 |
| 级联及接口 | SRL 720 条、其他专用级联 4,419 条无违例；1,286 个固定接口及 10,042 条保护/时钟/端口合同记录保持 |

原始结果及可机器读取的来源见 [版本来源记录](../provenance/hardware-resource-adaptation-20261104.json)。完整布通仅证明此次具体布局可以被 Vivado 路由，不证明 AMF 的普适半区规则已经完备。

最终 DCP 只保存在上述服务器目录的 `reports/llama_routed.dcp`，SHA256 为 `1498b4a8ee231b820e8b834c19437570914a4e4845ad3131426524b3f5c3bc4c`。没有将 DCP、二进制或运行缓存加入 Git。

## 配置与耗时口径

本例保持 3.332 ns 主周期和原派生时钟、普通 Pro、`ProPrimitiveTiming=false`、`SllRegionPolicy=preserve`、AMF 12 线程、Vivado 2026.1 四线程。`SllRegionPolicy=release-single` 仍为已实现但当前不启用的实验策略；它取消符合条件单体 PU 的区域偏好及对应牵引/裁侧，独立于 SLL 数量目标。

已有 AMF 实测墙钟 2,545.332 s，适配 38.050 s，Vivado placement 后端兼容处理 1,555.156 s。独立布线调用 1,925.842 s；该次布线续跑的进程墙钟为 2,820.640 s，包含打开 DCP、审计、报告与写出，监督器以 5 秒间隔观察退出。分阶段恢复运行没有一条从 AMF 启动至最终布线交付的连续实测总墙钟，不能将跨轮累加或“扣除 Vivado placement”的指标写成实测总墙钟。

## 保留的限制与后续工作

1. **SRL 打包上限仍为 4。** `SRLMaxPerSLICEM=4` 的共享打包策略未扩展到 8。长级联段最多 8 个成员和多个独立 SRL 的 share4 是不同路径，不能相互替代。硬件具有 8 个可用 LUT，不意味着当前 AMF 已具备任意 8 个 SRL 的兼容打包。
2. **CLB 内部利用率仍待改善。** 当前普通逻辑打包、LUT 配对以及 memory site 混放等损失已有诊断；本版未新增紧凑化算法。
3. **错误区域选择仍待改进。** 长组合簇的区域规划可通过不可拆 LUT–FF PU 牵连 FF，而其高扇出后级可能规划到远处。此次扩散顺序修复解决预算冲突，没有解决这类区域决策质量。
4. **高扇出关键路径仍待优化。** 驱动复制次数、上级负载变化、复制和布局阶段的形成过程继续按实际网表与日志追踪；本版未新增复制策略或拓扑优化。
5. **固定复合结构并非无限通用。** 以本例实际出现的原语与连接为验证范围；未知固定多成员宏、分叉 URAM 等仍明确拒绝。不能据此声明所有器件、DDR3 或所有 IP 已完整适配。
6. **8 项半区规则争议暂缓。** 例外仅适用于匹配已批准记录的具体 carry/half、site 和占用单元，未放宽 Vivado DRC，也未生成通用豁免规则。

## 提交与验证对应

| 提交 | 内容 |
| --- | --- |
| `6380c11` | 全设计接口、I/O 时序、同步链、MUX 与 SRL 边界适配 |
| `76070fc` | URAM 级联与完整 FF 控制极性 |
| `65e9bfa` | AND2B1L 组合传播 |
| `14d07af` | 先区域纠正，再建立共享扩散预算 |
| `73fab09` | CARRY 槽位归属诊断入口 |
| `7f27436`、`a05c832` | DI 唯一归属轻量修复与既有槽位提升回归 |
| `90a98c7`、`be9a332` | ASYNC 目标声明与属性恢复事务 |
| `284664c` | LUT6_2 内部配对保护 |
| `540d007` | CARRY 半区直通预留 |

各批次的专项构建、真实网表检查及回归记录保存在原独立副本父目录。例如 CARRY DI 检查覆盖 12,079 个宏、16,458 个 CARRY，单独/成对集合重叠 528→0、重复槽位 544→0；半区修复验证包含 12 个边界用例、3 个既有 CLB 用例、9 个 DI CARRY 及 llama/GETRF 实际网表对比。它们是历史验证结果，不能当作本次归档又执行了一轮完整布局布线。本次验证是源码树一致性、Git 对象可达性和文档/来源完整性检查。
