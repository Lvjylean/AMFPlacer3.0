# llama U250：URAM 级联与 FF 控制极性

本轮在第一批提交 `6380c11be2536dcd7507e9142d469c1be8537215` 的独立副本继续开发。原 SA 跨 die 补偿仓库、第一批实验和冻结 DCP 保持原样。未推送。

URAM 级联按实际 `CAS_OUT* → CAS_IN*` 连接去重并识别线性链，新增可移动 URAM 宏。成员按级联方向使用真实器件行距；现有列合法化和 DP 分配连续、同列、同 SLR 的可用 site。分叉、自环、环、跨 RP 接口和未覆盖的固定多成员宏明确拒绝。普通 URAM 数据连接仍允许两个独立单元分处不同 SLR。宏序列化、非 CLB site 登记、锚点恢复和最终 Vivado 位置导出均包含 URAM 成员。原接口专项入口的 URAM 异常绕过已删除。

完整控制元数据从同一准备 DCP 的引脚 `IS_INVERTED` 批量提取，记录每类原语总数和全部反相控制引脚；与冻结网表的规范单元清单核对后生成每个单元的显式记录。不能用可缺省的单元属性或只列冲突 FF 来代替覆盖证明。标准 FDRE/FDSE/FDCE/FDPE、LDCE 与使用锁存器资源的 AND2B1L 均有对应控制引脚映射；当前未支持反相 CE/GE，出现时明确拒绝。

控制集 ID 包含 CLK、SR、CE 网络、控制类型及极性。共享一个半 SLICE 的两个 CE 分组还须匹配 CLK/SR 极性及同步/异步/锁存器模式。规则覆盖初始 Carry 关联 FF 选择、候选插入、拷贝、移除、FF 交换、受阻 Q 引脚容量检查和最终 BEL 检查。修正原 `getFFSRType` 的布尔返回类型，避免类型 2、3 都变为 1。保留已有首次适配的分组搜索顺序，没有引入新的打包优化目标。

完整接口模式必须提供完整控制元数据。缺失、重复、类型不符及不支持的极性均拒绝；Python 正式入口核对二进制能力，并把控制文件哈希与输入 DCP 来源记录绑定。旧实验输入文件不原地改写。

验收入口位于本副本父目录 `validation/`：

- `batch-2-final-regression-20261103-021308`：18 项硬资源场景、FF 实际生产打包、专项范围及 I/O 时序回归通过。涵盖 URAM 接缝、缺口、分叉、环、资源竞争、独立 URAM 跨 SLR 数据连接，以及 DSP/Carry 既有规则。
- `batch-2-acceptance-regression-20261103-022004`：最新构建的 FF 控制/BEL 测试、41 项 Python 测试、3 个 MUX 场景和 8/9/17 级 SRL 边界场景通过。
- `batch-2-ff-export-20261103-021807`：657,061 个普通 FF 与 4 个 AND2B1L 的完整控制记录；62,391 个普通 FF 的复位控制反相，当前没有时钟反相。
- `batch-2-case-validation-20261103-022213`：真实完整网表正式初始打包通过；4 条三级 URAM 链、1,785 个同步组、5,347 个 MUX 宏通过检查。硬资源阶段成功分配 18,091 个逻辑单元：16,458 CARRY8、1,248 DSP48E2、64 RAMB18E2、289 RAMB36E2、32 URAM288；8 条 URAM 级联边均满足相邻同 SLR。
- `batch-2-production-export-20261103-022750`：链接冻结构建的生产库，验证 URAM 非 CLB site 登记、整条宏的锚点恢复、最终生产 Tcl 导出和锁存器/AND2B1L 控制模式编号。通过。
- `batch-2-uram-vivado-replay-20261103-023043`：真实 DCP 中回放 URAM 位置的结果以其 `status.json` 和 `audit/result.json` 为准，最终批次报告汇总。

验证构建：`builds/build-20261103-021806-879551-6380c11b/build/AMFPlacer`。构建记录以第一批提交加工作区快照追溯；最终批次记录核对生产源码与冻结构建逐文件一致。

默认周期仍为 3.332 ns，全部派生时钟、IP/板卡约束、普通 Pro (`ProPrimitiveTiming=false`)、`SllRegionPolicy=preserve` 以及已有 U250 坐标/SA/SLL 参数保持原值。`release-single` 仍是已实现、当前未启用的实验策略。

本轮不包含全局布局、全设计 CLB 最终合法化、完整 Vivado placement/routing 或时序收敛验收。初始打包和硬资源阶段成功不替代这些后续验证。

后续需单独处理的已知问题：这 4 个 AND2B1L 是组合逻辑，但旧 AMF `DesignCell::isTimingEndPoint()` 仍通过 `isFF()` 把它们作为时序边界。本轮补齐其物理控制兼容性，没有改动这种时序分类，也没有把它们当作普通寄存器的正确 STA 模型。下一步时序图检查应覆盖 DI/SRI 到 O 的组合传播。该问题不同于本轮 URAM 级联和控制极性阻塞。

架构依据：[UG573 UltraRAM Cascade](https://docs.amd.com/r/en-US/ug573-ultrascale-memory-resources/UltraRAM-Cascade)、[UG573 Cascading UltraRAM](https://docs.amd.com/r/en-US/ug573-ultrascale-memory-resources/Cascading-UltraRAM-and-Matrix-Configurations)、[UG574 Control Signals](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Control-Signals)、[UG974 AND2B1L](https://docs.amd.com/r/2024.2-English/ug974-vivado-ultrascale-libraries/AND2B1L)。
