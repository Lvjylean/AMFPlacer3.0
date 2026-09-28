# AMFplacer3.0 多 die 剩余工作评估

评估日期：2026-09-27。只读核对服务器 `/Projects/jinyang/workspace/AMFplacer3.0`，分支 `codex/clb-import-legality`，提交 `d534237b`，核对时工作区干净。本次新增调研文档，不修改算法、不启动实验。以下优先级和模块名属于实施建议，不代表用户已批准开发。

需求依据为 `docs/requirements/task-3-final.pdf` 第 12–15 页、1.2 节和图 6。已核对其 SHA-256 与用户原始 PDF 一致：`451302ba0423ea3d2e5b71394861239ac6fee6a1d8141801fcffebfd24d7d67b`。公开资料优先采用与当前后端一致的 AMD Vivado 2024.2 文档。

**当前判断**：项目已经具备 U250 / GETRF 的多 SLR 全量布局及 Vivado 后端验证能力。主要剩余工作是把多 die 的资源、时钟和路径关系纳入整个布局决策过程，并实现跨版本增量复用。SLL 容量和 CLB 布线拥塞另列专项，本报告不重复展开。

**已经完成、应继续复用的能力**

| 能力 | 已有证据与边界 |
| --- | --- |
| 输入和器件信息 | 第一步按用户确认范围完成；U250 真实资源、SLR 归属和禁用 site 已导入，URAM 有独立资源分配与合法化 |
| 物理结构模型 | 识别 U250 三条 SLR 接缝和内部 HPIO 带，形成 8 个区域，统计各类容量；不将普通时钟区域边界一律视为障碍 |
| 多 die 时延启发式 | 物理模式使用 SLR/HPIO 边界罚项；当前 1.5 ns/SLR、0.5 ns/HPIO 仍是启发式参数，已有后端采样工具 |
| 二维聚拢 | 关键路径段、区域资源预留、X/Y 软目标及移动后 STA 反馈已实现；直接移动对象限 CLB PU，硬资源参与评分但不由该聚拢器搬移 |
| 合法性及后端 | Carry/DSP 等专用级联审计、CLB 打包与 BEL 冲突修复、严格导入及结果对账已具备 |
| 全量验收 | 最新 delay 轮 856,998 个目标 cell 在导入、placement、routing 后保持原 LOC/BEL；布线完整，DRC 错误/严重警告为 0，WNS +0.111 ns，WHS +0.023 ns |

实验范围必须保留：最新 control/cluster 只完成严格导入；修复前 A–D 三组有完整布线对照。不能把不同源码和验收阶段混为一组性能比较。二维聚拢尚未证明优于仅延迟模式。现有 GETRF 为 OOC 输入，时钟来源和端口约束限制仍在，不能代表板级实现已完成。详见 [严格导入验收](clb-import-legality.md) 和 [物理边界实现](device-physical-boundary-implementation.md)。

**1. 真正以 SLR 为单位的初始分配：优先实现**

当前 `ClusterPlacer`、PaToH 和 `SAPlacer` 仍主要采用 2.0 的通用聚类、二维时钟区域网格和几何代价。后续已有区域容量检查，不等于初始分配已按 SLR 多资源联合求解。

建议为每个簇计算 LUT、FF、SLICEM、CARRY、MUX、DSP、BRAM18/36、URAM 的需求向量和可放置 SLR 集合，联合选择 `cluster → SLR → region`。扣除固定对象、禁用资源和已承诺的占用，使用可配置余量。BRAM18/36 和 SLICEM/LUT 的共享关系必须继续去重；宏的连续站点需求也要检查，不能只看总量。

目标包括资源可行、关键连接延迟、跨 die 往返和固定端点距离，而不是强制各 SLR 相同比例。对容量不足或形状不合适的簇，支持拆分、交换及重新分配。用户的独立 partition/floorplan 项目已有相关能力；当前继续遵守“先只修改 AMF、暂不接入外部结果”的范围。

计划书（2a）明确写出改进的 min-cut ILP。可先用现有 PaToH/SA 生成可行初值，但若按该算法路线验收，仍须实现 ILP 约束、目标和求解，或明确记录采用了替代方法，不能把 PaToH 记作 ILP。

验收：在全局布局前输出每簇归属、逐 SLR 多资源利用率、宏候选列及不可行原因；容量或宏形状无解时在此阶段报告；真实 GETRF 和额外 case 上与现有初始解对照。AMD 同样强调逐 SLR 检查各类资源，整片平均利用率不能替代局部可行性。[UG949：SLR 利用率](https://docs.amd.com/r/2024.2-English/ug949-vivado-design-methodology/SLR-Utilization-Considerations?contentId=q1SRVF~yArFOih~ub5M3Tw)

**2. DSP/BRAM/URAM 与周围逻辑协同布局：优先实现**

URAM 支持及硬资源合法化已经完成；缺口在于硬资源和周边 LUT/FF 的多 die 联合优化。当前二维聚拢直接排除了 DSP/BRAM/URAM，不能通过一次候选移动重新协调整段混合资源路径。

建议定义“硬宏及其局部逻辑”的优化分组，对每个候选 SLR/资源列估算可行站点和所有受影响边的时序代价；通过整体移动、硬宏交换或局部回填进行优化。专用级联必须保持连通规则，普通分组允许在时序有利且资源合法时跨 die。连续列、长级联和宏形状约束应前移到初始分配，不留到末期打包才发现。

验收：BRAM/URAM/DSP 主导的关键路径是否减少不必要的跨 die 往返；相关单元全部合法；硬宏周围逻辑和最终后端时序共同受益。对应计划书（2a）、（3b）、（4）、（5）。

**3. 校准多 die 时延模型，并增强时序约束语义：优先实现**

已有距离回归、边界罚项、轻量 STA 和布线后样本导出。缺口是 U250 数据校准和比单个周期更完整的路径要求。当前时序图主要传播 latest arrival，按 `ClockPeriod` / `ClockPeriod:<driver>` 给出要求；未发现完整的多时钟关系、false path、multicycle、min/max、setup/hold 双向分析实现。

建议扩充按距离、资源/引脚类型、扇出、跨界组合分类的样本，先诊断原模型残差，再拟合；使用独立设计或留出路径验证，不能只在 GETRF 关键路径上调参。记录模型版本、适用器件和预测误差，比较关键路径排序及候选移动收益与 Vivado 的一致性。

用户提供的约束始终优先。扩展 placer 的约束输入接口即可，自动提取 DCP/XDC 仍是可选辅助，不重新打开已关闭的第一步。支持多时钟时，需要标明哪些路径被计时、哪些受例外约束，以及不支持的语义；CDC 同步器属性和聚拢要求也应保留。

验收：独立样本的误差与关键路径排序改善；多时钟、例外路径和 hold 微型 case 的分析与 Vivado 对照。对应计划书（2b）、（6）。AMF 2.0 已有时序回归、障碍锚点和时序驱动 QP，不应将这些基础模块重新计作新实现。[AMF-Placer 2.0 原论文](https://arxiv.org/abs/2210.08682)

**4. 时钟网络与 hold 风险：多 die 必需的完善项**

当前已有 clock-region/clock-column 利用率统计与供给调整、FF control-set 合法性检查。因此不是完全没有时钟约束。仍需补充与真实 BUFG/MMCM/PLL、时钟根位置、负载分布及跨 SLR 时钟偏斜相关的评估，并核对继承的资源阈值适用范围。

数据路径变短不保证 setup 和 hold 同时改善。跨 die 时钟到达差和变化范围会影响可用时序预算；不能全部归入一个固定的数据线惩罚。可先导出 Vivado 时钟利用率、clock skew 和 hold 报告，用简化模型指导区域分配；时钟网络的具体构建、精确校正与最终时序签核继续由 Vivado 完成。

验收：合法的时钟负载分布、无新增时钟规则违规、setup/hold 同时对照；新增一个真实多时钟测试，不能仅依赖 GETRF。AMD 文档说明时钟根相对位置及行可编程延迟会影响 SLR crossing 的 skew 与 hold 修复。[UG949：UltraScale/UltraScale+ 时钟延迟](https://docs.amd.com/r/2024.2-English/ug949-vivado-design-methodology/Reducing-Clock-Delay-in-UltraScale-and-UltraScale-Devices)

**5. 路径级跨界规划与 Laguna 寄存器映射：第二阶段优化**

现有路径段聚拢和跨界审计已能发现往返，仍缺少整条流水线的 SLR 顺序及过界位置联合选择。建议识别寄存器级段，减少不必要的 `SLR0 → SLR1 → SLR0`，结合每段 slack 和两侧资源选定过界位置，避免只追逐某一条边的局部收益。

进一步为符合规则的既有 FF 提供 Laguna TX/RX 候选、成对资源预留、控制集检查和 BEL 分配。这属于跨界寄存器映射，不等同于 SLL 容量/拥塞建模。仅有 `USER_SLL_REG` 软属性不足以保证当前“全部位置由 AMF 严格确定”的流程采用该映射；需要 AMF 明确分配，或另设可审计的后端协同模式。

先覆盖已有寄存器的物理移动。自动加流水级或 retiming 会改变时序/延迟契约，不属于默认的纯 placer 功能，须有独立功能保持设计和验证。UltraScale 与 UltraScale+ 的规则不同，不能复制一套硬编码规则用于所有板卡。[UG949：SLR crossing registers](https://docs.amd.com/r/2024.2-English/ug949-vivado-design-methodology/Using-SLR-Crossing-Registers)；[UG912：USER_SLL_REG 2024.2](https://docs.amd.com/r/2024.2-English/ug912-vivado-properties/USER_SLL_REG)

验收：单扇出同钟寄存器链等受支持 case 的严格映射与 hold 检查通过；不支持的控制/连接组合保留普通 FF 布局；路径级跨界次数与完整后端 QoR 一同报告。AMD 的软 SLR 和 crossing 约束也强调保留寄存器移动自由度及避免无意的往返。[UG949：软 SLR 约束](https://docs.amd.com/r/2024.2-English/ug949-vivado-design-methodology/Using-Soft-SLR-Floorplan-Constraints)

**6. 聚拢、扩散、打包共享区域决策：第二阶段优化**

当前聚拢有区域预算和 X/Y 软目标；扩散允许离开偏好区域，最终打包以合法性为准。这是合理的软约束，但不同阶段尚无统一的长期 SLR 归属与迁移事务。最新控制组暴露并修复的时钟列搜索停滞，也说明偏好与合法站点搜索需要统一解释。

建议建立可拆分的内部模块/分组及区域偏好接口，供初始分配、QP、扩散、硬资源合法化和详细打包共同读取；统一区分固定约束、软偏好和暂时预留。跨区域迁移需更新资源账本，并记录原因。增加迁移收益门槛、冷却或回滚，减少多阶段反复跨界。

即使不建模布线拥塞，也应从 control-set 碎片、SLICEM 占用、宏形状和可打包性估算“有效可用容量”。当前合法性修复属于基础，不保证初始数量预算都能实现。对应计划书（3c）、（3d）的非拥塞部分、（4）、（5）、（8）。

验收：每阶段可核对容量和归属；不存在双重预留、冻结资源被占用或无法解释的迁移；cluster 模式须在完整后端对照中证明收益，不能以内部估计下降替代验收。

**7. 高扇出与真实固定端点：工程与质量完善**

高扇出控制信号跨多个 SLR 时，简单平均距离不足以反映源端与多个负载群的关系。先实现按 SLR 分组的负载诊断和不改变功能的布局调整；驱动复制、缓冲或物理综合可作为单独的 Vivado 协同实验，并追踪网表变化，不在严格位置保留基线里悄悄加入。[UG949：高扇出网络](https://docs.amd.com/r/2024.2-English/ug949-vivado-design-methodology/Optimizing-High-Fanout-Nets?contentId=cLMJnqbOgBRU4xWU37zs5A)

完整平台还应处理真实 I/O、DDR/PCIe/其他固定 IP、预留区域和禁止区域对内部逻辑的吸引及限制。目前导出 HPIO/硬 IP 的物理位置不等于已验证所有端口、时钟和硬 IP 连接约束。用具有真实端口和时钟约束的 case 扩大验收；只有目标扩展到板级交付时，再完成计划书（9）的 bitstream 验证。

**8. 按 SLR/模块并行求解：计划书明确缺口**

已有 OpenMP、多线程 QP/扩散和并行搜索基础。计划书（3b）、（3c）所要求的各芯粒粗粒度模块化并行，仍需要 SLR/模块任务分解、边界端点状态、全局协调与资源提交。

建议稳定初始分配后，再让各 SLR 在共同边界状态下局部求解，周期性更新跨 die 连接和迁移。不能简单将四个 SLR 各跑一遍单 die placer：跨 die nets、共享约束和迁移必须协调。验收应在固定线程总数与独占资源条件下比较墙钟时间和 QoR，同时检查重复运行的一致性。对应计划书（3a）调度及（3b）、（3c）。

**9. 跨版本增量编译：完成课题目标的独立主线**

当前 `IncrementalBELPacker` 在一次布局中做 LUT/FF 等配对；不是 V0→V1 的增量编译。计划书（7）的增量打包有基础，不能用它代表（3a）的变更感知控制器已完成。

需要依次实现：

1. `DesignSnapshot / NetlistDiff`：稳定对象匹配，记录功能参数、连线、时钟/物理约束和器件/模型版本，拒绝不兼容缓存。
2. `ImpactAnalyzer`：活动、缓冲和冻结对象；跨 die 边界依赖与变化后的资源重新预算。
3. 局部布局、合法化与重打包：冻结对象及其资源不可被侵占；按需扩域，失败有界回退。
4. 增量时序更新及缓存：只重算受影响部分，保证依赖失效正确。
5. Vivado 后端复用：验证参考 DCP 与 AMF 新位置的配合，分别报告 cell 位置复用和 routed-net 复用，不能用位置复用率替代布线复用率。Vivado 已有增量实现能力，可作为后端与对照；仍需实验证明接口兼容与收益。[UG904：Incremental Implementation](https://docs.amd.com/r/2024.2-English/ug904-vivado-implementation/Incremental-Implementation)

增量工作的参考 routed DCP 是缓存状态；用户给 placer 的当前设计输入仍可保持综合/优化后的网表。这不意味着改成以已完成布局布线的 DCP 作为正常初始网表。

验收：无变化、参数变化、连接变化、增删单元、约束变化、跨 SLR 影响及缓存不兼容分别测试；同一 V1 全量/增量对照，分开记录 AMF 和端到端加速。本主线可从单 SLR 的小型 V0/V1 开始，不必等所有多 die 优化完成。

**10. 器件泛化和可复现评估：贯穿所有阶段**

当前几何支持纵向堆叠 SLR 和贯穿 I/O 带；导出所有 site/tile 不会自动生成全部架构规则。建议为支持的架构建立版本化的资源、专用连接和可用性规则，拒绝尚不支持的拓扑；新增另一种器件时单独校准和验收，不把 U250 结果泛化到任意多 die FPGA。

建立包含单 die 回归、多 SLR、URAM/DSP 密集、多时钟、高扇出、固定 IP 和跨版本变化的测试集。每项优化都同时检查严格导入、DRC、布线完整性、WNS/TNS、WHS/THS、位移/复用及墙钟时间。运行期 profiling、设备数据与网表导出缓存、减少无变化的重复 STA 也是现实的提速候选；先测量再优化，不能从一次共享服务器实验推断加速比。

功耗/热感知、自动流水化、自研 router、HBM/Versal 全架构支持可作为后续研究，不属于当前 U250 多 die placer 必须同时完成的范围。

**建议实施顺序**

| 顺序 | 工作包 | 主要交付和停止条件 |
| --- | --- | --- |
| 1 | SLR 多资源初始分配及硬宏可行性 | 明确的簇归属、资源账本和宏候选位置；无法容纳时可解释地拆分/报告 |
| 2 | 硬资源与 CLB 联合优化，同时校准多 die 时序 | 在完整后端验证改善，不增加合法性错误；校准样本与评估样本分离 |
| 3 | 时钟/hold 完善、路径过界规划和 Laguna | 先支持明确子集，逐项通过时钟、控制和 setup/hold 测试 |
| 4 | 稳定的区域决策、扩散/打包协同和并行求解 | 资源一致、减少无效迁移；独占条件下证明时间或 QoR 收益 |
| 独立主线 | V0/V1 快照、差异、冻结和后端复用 | 先小设计，再扩展多 SLR；以同一 V1 全量对照验收 |

这里的“独立主线”是工程顺序建议，不表示已启动多个开发任务。SLL 和 CLB 拥塞专项可以在共同资源账本及分配接口建立后接入。

**源码落点与核查证据**

以下均相对服务器项目根目录，行号对应 `d534237b`。新增模块名只是建议。

| 工作包 | 已有代码及证据 | 建议扩展 |
| --- | --- | --- |
| 初始分配 | `src/lib/HiFPlacer/placement/globalPlacement/ClusterPlacer.cc:687` 汇总 CLB/DSP/BRAM；`:716` 向 SA 传入 CLB 权重及二维网格；`src/lib/3rdParty/partitionHyperGraph.cc:44` PaToH 的 net weights 为 NULL | SLR 初始分配器、资源需求向量、宏可行性目录；扩展 `GraphPartitioner`、`SAPlacer` |
| 统一器件与资源 | `src/lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.h:15` 列出 11 类资源；`DeviceInfo.cc:196` 起检查纵向 SLR 行映射 | 扩展现有模型和区域账本，避免另建一套不一致容量统计 |
| 硬宏与聚拢 | `src/lib/HiFPlacer/placement/placementTiming/BoundaryAwareClusterer.cc:135` 排除固定/锁定和 DSP/BRAM/URAM | 与 `src/lib/HiFPlacer/placement/legalization/MacroLegalizer.cc` 联合评估和提交候选 |
| 时延与约束 | `src/lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.h:122` 边界代价；`PlacementTimingInfo.cc:30` 周期输入；`PlacementTimingInfo.h:403` latest arrival 状态 | 器件校准数据、约束视图、时钟/hold 风险及路径级评估 |
| 时钟利用率 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc:1398` 时钟供给调整；`:2076` 利用率检查 | 与实际时钟资源/根/负载分布核对并补充跨 SLR 评估 |
| 扩散和打包 | `src/lib/HiFPlacer/placement/globalPlacement/GeneralSpreader.cc:430` 软目标允许扩散离开；`src/lib/HiFPlacer/placement/packing/ParallelCLBPacker.cc` 执行详细打包 | 共同的区域偏好、容量承诺和迁移事务，完善打包可行性估算 |
| 增量编译 | `src/lib/HiFPlacer/placement/packing/IncrementalBELPacker.cc:32` 为单轮 LUT/FF 配对 | 新增快照、差异、影响分析、增量控制器；接入 `scripts/amf3.py` 与后端复用 |

全文 C/C++ 及执行脚本核查未找到生产路径中的 Laguna/USER_SLL_REG 分配、完整时序例外解析、DesignSnapshot/NetlistDiff/ImpactAnalyzer 或 `read_checkpoint -incremental` 接入。该结论结合实际数据结构与调用点，不依据单纯的文件名判断。

历史 `step-02-topology-floorplanning-assessment.md`、`roadmap.md`、`traceability.md` 中仍有部分移植前状态，不能覆盖本次已核实的 U250 能力。计划书是设计路线和需求依据，不能把其中“计划实现”或年度总结的表述直接当作本仓库已经实现的证据。
