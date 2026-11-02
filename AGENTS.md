# Current isolated revision: region correction before spreading budget (2026-11-03)

The user authorized region normalization before the shared 20-bin spreading origin, separate correction-distance accounting, and a new complete llama U250 acceptance run. Base 65e9bfa and historical failures remain unchanged. Use 3.332 ns, ordinary Pro, ProPrimitiveTiming=false, SllRegionPolicy=preserve, AMF 12 threads and Vivado 2026.1 four threads. Source changes are confined to this copy. Full acceptance must follow successful focused regressions; logs and results live in the parent validation and experiment directories. See docs/research/llama-region-before-spread-20261103.md. Statements below refer to their historical dates.

# 当前独立副本：AND2B1L 组合时序修复（2026-11-03）

用户要求单独修正组合逻辑被当作端点的问题。本轮基于 76070fc，仅从时序端点分类中排除 AND2B1L，保留物理 FF/锁存器资源、SRI 极性及打包规则。专项前后向传播/关键路径测试、既有 I/O 和 FF 控制回归、真实 llama 初始打包及两个图中的 4 个 AND2B1L 核验通过。主周期仍为 3.332 ns，既有时钟和物理约束不改；未运行全局布局布线。详见 docs/research/llama-u250-and2b1l-timing-20261103.md 和父目录 reports/and2b1l/。下文 AND2B1L 未修复的说明保留为上一批历史记录。

# 当前独立副本：URAM 级联与控制极性（2026-11-03）

用户明确要求处理第一批的两项阻塞。本轮在第一批提交 6380c11 的独立副本继续：新增可移动 URAM 线性级联宏及合法化/导出，完整 FF 控制极性导入及共享半 SLICE 兼容规则。正式真实网表初始打包与硬资源合法化已通过。原仓库和第一批记录不修改，不推送；完整布局布线尚未执行。详见 docs/research/llama-u250-uram-controls-20261103.md 和父目录 reports/batch-2/。

新增已知时序问题须保留记录：本例 4 个 AND2B1L 的物理控制已覆盖，但旧时序图仍将其当作端点，下一步需核对组合传播。本轮不宣称全设计时序建模已完成。以下阻塞说明按历史日期理解。

# 当前独立副本：U250 全设计接口适配（2026-11-03）

本副本基于第 0 批冻结源码 `0a78b00f7cdc56f4428c656256f7462491d0244d`，分支 `codex/llama-u250-interface-adaptation-20261102`，位于 `experiments/preflight/20261102-llama-u250-interface-adaptation/repo`。原 SA 跨 die 补偿副本与历史实验不修改。以下旧任务标题和限制按各自日期理解。

当前第一批包括：以实际 case 为依据的 U250 输入适配、E4 MMCM/PLL 与 DDR/PHY/PCIe/GT/时钟固定接口、复合 I/O 的规范单元边界和双向 pad 侧记录、后端恢复及保护约束核验、ASYNC_REG 同控制链的可移动同 SLICE 打包。I/O 在 AMF 内作为独立发出/接收的时序边界，数字路径按适用周期评估；报告称估计延迟/余量，不称 Vivado slack。默认周期 3.332 ns，保留 benchmark 派生时钟和 Vivado 约束；`ProPrimitiveTiming=false`、`SllRegionPolicy=preserve`，继承既有 U250 坐标及 SA/SLL 参数。

已确认下一批完整布局阻塞：本例 4 条三级 URAM 级联尚未被硬资源合法化器支持；全设计存在 21 类同 CLK/SR 网络但复位极性不同的 FF，通用控制集与混合打包需要显式覆盖极性。ASYNC_REG 组内区分极性不等于全设计混合打包已支持。不得跳过这些检查启动完整布局。

第一批验收只到实际输入加载、初始打包和单个组的 BEL 分配、时序图及固定接口恢复；不能把这些结果当成完成全局布局、完整合法化、布线或时序收敛。后续完整实验另设独立目录。DCP 只留服务器。新增源代码和全部验证入口均在本副本中；小型或完整验收结果见父目录 `validation/` 和最终批次记录。尚未验证的新 case 或固定 fabric 宏必须明确拒绝，不能静默释放约束。

# 当前独立副本：U250 SA 跨 die 纵向补偿（2026-11-02）

用户接受同列、相邻 CR 跨 die 完整分支中位数 1.415 ns 作为单点标定。本副本基于698c7845，分支codex/sa-cross-die-compensation-20261102。仅普通 U250 SA 默认启用，SACrossDieCompensationEnable=false可回到基线；VU095/VCU108及DFX原样保留。按当前模型换算总等效Y=182.69021394，减掉原来已计的一行距后，每道真实SLR接缝增加剩余纵向距离；横向项和连接权重保留。不是新二维拟合，不把1.415解释为固定额外ns。只构建和小型验证，不启动完整布局布线，不推送。不得捎带修改多启动、容量、QP、SLL规划或时序模型。详见docs/research/sa-cross-die-compensation-20261102.md。以下为历史记录。

# 当前独立副本：SA 器件分派（2026-11-02）

用户授权从 b0c4ad1 新建代码备份，仅按现有 device 配置禁用 U250 的 SA 第三列水平距离附加罚，保留 VU095/VCU108 原行为。副本分支 codex/sa-device-policy-20261102。修改前归档在父目录 baseline-source.tar.gz。只构建和小型验证，不启动完整布局布线，不推送；不捎带修正 SA 多启动或新增容量/跨 SLR 成本。详见 docs/research/sa-device-policy-20261102.md。以下为历史记录。

# 当前 GitHub 发布：sll约束优化延后（2026-11-02）

用户授权将已验收候选 c425bb98737019694942cd5d8fd6b990ece4a91c 发布到 `codex/sll-edge-compact-snapshot-20261024`。独立发布副本 experiments/preflight/20261102-sll-deferred-release/repo 基于上次发布258d1178，生产树与候选逐项一致；仅额外增加发布说明。保留2.0提速和L分段修正。

`SllDeferredUntilFirstSpread` 默认false；显式true时首轮全部资源扩散后、下一次QP才启用虚拟SLL，数量梯度和tau时机不变。独立开关 `SllRouteConstraintEnable=false` 仅关闭位置约束导出，不关闭规划优化。逐帧记录只读planner，未启用帧标记uninitialized，不填充最终位置。最新已验收组合启用既有20bin限幅，但该限幅只对跨die搜索PU激活，不覆盖全部die内扩散；本次没有新增连接感知方向限制。

源码和验收范围见 docs/research/sll-deferred-start-20261102.md。旧实验保持原样，DCP只留服务器，不重新启动实验。下文“当前”按日期保留为历史状态。

# 当前 GitHub 发布：2.0优化加速版本（2026-11-01）

本版按用户要求同步到 `codex/sll-edge-compact-snapshot-20261024`，在独立发布副本 experiments/preflight/20261101-sll-speed-v2-release/repo 整理。已完成验收的生产源码为 1672cf86c4b029d2550f9d1a465ce12914b8321c，基于提速版1.0 d04315a3；发布提交仅补充文档，生产源码、脚本、配置、测试保持不变。网格维护与边界报告优化已完成完整等价核验，同配置 AMF 单次墙钟1291.3094→1231.9773秒。详见 docs/research/sll-speed-v2-20261101.md。

端点排序、进一步网格批处理/成员复用、扩散容器及更深层SLL稳定拓扑缓存仍未实施，不得声明全部提速建议均已完成。保持既有功能与参数规则；本轮性能验证无Vivado后端，不外推WNS或布通性。下文“当前”“尚未实施”等为按日期保留的历史状态，以本节及版本说明为当前入口。

# 当前等价提速尝试（2026-11-01，第二轮）

独立副本基于提速版1.0 d04315a3。用户授权继续优化并按原规则验收。本轮只改串行密度网格维护和只读边界计数，保持容量选择/遍历/报告/时序/规划决策。验收为小型差分与同配置 AMF-only：10ns、12线程、SLL全开、无可视化、CrossDieSpreadMaxBins=0。基准 AMF 1291.309400562197s。未通过完整等价验证前不声明成功。以下为历史记录。

# 当前 GitHub 发布：提速版1.0（2026-11-01）

本版按用户要求更新 GitHub 最近的 `codex/sll-edge-compact-snapshot-20261024` 分支，发布整理副本位于 experiments/preflight/20261101-sll-speed-v1-release/repo。已验证的算法源码来自 e9a21f594caab335c39fca80646a7f164781b30f，发布仅增加版本入口和轻量验证说明，保持算法、测试、脚本与配置原样。功能及计时范围以 docs/research/sll-speed-v1-20261101.md 为准；下文分支名称和实验路径属于历史。后续讨论的网格、边界报告和扩散容器优化尚未实施，不纳入本版。

# 当前 SLL 等价运行时优化副本（2026-11-01）

独立分支 `codex/sll-runtime-optimization-r2-20261101`，基于 `9f63b97b`。用户已授权两批等价性能优化及同配置 AMF-only 性能验证：关闭日志时跳过纯汇总、批次内缓存时序开关、批次外保留动态读取、限定生命周期的共享延迟前缀、复用收集缓冲、增加刷新分阶段 profile，并在既有线程内并行只读 slack 直方图扫描（整数汇总、原序写回）。保持刷新频率、公式、浮点顺序、容量与候选决策。当前测量配置 10 ns、12 线程、SLL 开、visualization=false、CrossDieSpreadMaxBins=0；不启动 Vivado。历史实验和源码不修改。下文为历史记录。

# 当前跨 die 扩散限幅副本（2026-11-01）

本副本分支为 `codex/cross-die-spread-limit-20261101`，基于 `6c0557f5`。用户已确认 `CrossDieSpreadMaxBins=20`，按标称网格宽高约束整轮跨 die 扩散净位移，参考点为 QP 后、扩散前。原整合分支与实验冻结输入不改。本任务只构建及小型验证，不自动启动完整布局布线；详见 `docs/research/cross-die-spread-limit-20261101.md`。以下标题和路径属于历史记录。

# 当前整合分支（2026-10-31，已合入 L 修正）

本工作区分支 `sll-增量合并与加速` 已快进合入 `417484ec` 的 L 分段实现及 `8ec2cb36` 的公式和验证说明。发送端到 SLL 入口（多边界时为上一出口到下一入口）独立计为二点连接，本地和远端接收端仍分别采用共享超边；初始化、QP 和线长缓存一致。以下“原分支未修正”和“不实施 L_next”仅为历史归档记录，不再描述本分支当前状态。既有增量编译、审计及后端默认策略保持。新逐帧可视化扩展仍位于其独立分支，本次未合入。

# 历史 L 修订副本（2026-10-30）

本副本已按用户授权拆分 SLL 输入段与本地接收端超边，见 docs/research/sll-wirelength-split-20261030.md。新分支 codex/sll-wirelength-split-20261030；原 sll-增量合并与加速 分支仍为未修正基线。下文“不实施 L_next”仅描述历史归档任务，不适用于本次修订。10 个模型程序和 3 个集成测试通过；未运行新的完整 GETRF 实验。

# 当前归档约定（2026-10-30）

用户指定功能分支为 `sll-增量合并与加速`。当前功能与来源以 `docs/research/sll-integrated-snapshot-20261030.md` 为准，下文历史副本路径不作为当前入口。SLL 线长建模存在已确认问题，详见 `docs/research/sll-modeling-known-issue-20261030.md`；本次仅归档现有功能，不实施 L_next。后续修改应独立验证。默认 place_design 不调用防驱动复制的 DONT_TOUCH 拓扑保护；相关补丁仅历史留档。CARRY 预连接为特定 GETRF 可选覆盖包，不无条件应用于其他设计。脚本快照生成采用已验证的 support 文件打包版本。

# 当前副本说明（2026-10-30，DFX 短期兼容）

本副本位于 `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261029-dfx-native-interface-fallback/repo`，分支 `codex/dfx-native-interface-fallback-20261029`。用户当前只要求增量编译短期可用，暂不作为主要性能优化方向。本副本将 DFX 接口交给 Vivado 原生路由，内部 net 保留 SLL 约束及审计；详见 `docs/research/dfx-native-interface-fallback-20261029.md`。下文历史副本路径仅供追溯。

# 当前副本说明（2026-10-29）

本副本为 E2 与增量编译集成分支，路径为 /Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261029-e2-incremental-merge/repo。当前工作以本副本及 docs/research/e2-incremental-integration-20261029.md 为准；以下历史路径保留供追溯，不能据此覆盖原副本。新增验证必须使用独立实验目录，DCP 只保留服务器。

# AMFplacer3.0 开发约定

- 2026-10-19 本 78 副本新增 BoundaryClusteringStrategy=cr-slr-hierarchical，与 slr-hierarchical 同级，默认不变。中间 SLR 使用整个 cluster 投票 cells 的平均 Y 选择一个边缘 CR 中心，统一方向保持至重建聚类；越过锚点不反拉。保留阶段、硬约束及 74q/77 修补；仅构建和小型验证，不启动完整实验。详见 docs/research/cr-slr-hierarchical.md。

- 2026-10-19 本 77 独立副本将增量配置键规范为 `IncrementalCompilation`，旧拼写 `IncremenralCompilation` 仅兼容读取；新旧键值冲突即报错。配置生成只写正确键名，布局算法和默认开关不变。详见 docs/research/incremental-compilation-key.md。

- 2026-10-19 本 76 独立副本将 BoundaryClusteringStrategy 的缺省值设为 slr-hierarchical（原名 paper-hierarchical，旧名只作兼容别名）；region-gain 仅显式指定时进入。BoundaryAwareClustering 关闭时仍不启动新版聚类。BoundaryStageMode 默认 independent-max2，74q 修补和阶段算法不变。仅构建与小型验证，不重跑完整实验。详见 docs/research/slr-hierarchical-default.md。

- 2026-10-19 本 75 独立副本覆盖下文历史版本切换方式：在 74q 基础上用 `BoundaryStageMode=independent-max2|merged` 选择独立最多两轮或共同作用，默认 independent-max2。只有 paper-hierarchical 聚类开启时生效；74q 默认配置不变。保留 74q 的增量修补及纵向拉伸关闭状态。此次仅编译和合成单元验证，不运行完整实验、不提交；详见 docs/research/boundary-stage-mode.md。

- 项目处于 `3.0.0-dev`：保存经过验证的 AMFPlacer 2.0 基础，并逐步实现课题三的 FPGA 增量编译器。未实现的模块必须明确标注计划状态。
- 正式服务器根目录：`/Projects/jinyang/workspace/AMFplacer3.0`，通过 `ssh eda072` 以 jinyang 使用。
- 使用 `scripts/amf3.py` 构建和运行。构建目录独立，禁止把旧 `build.sh` 的清空逻辑当作默认构建方式。保留用户源码修改和失败实验。
- 新分支默认使用 `codex/` 前缀。记录当前源码提交、工作区变更、输入哈希、工具版本和构建目录。Git 中不存放 DCP、构建产物、运行日志、许可证或大型运行缓存。
- 最终 DCP 留在服务器 `experiments/runs/<run-id>/reports/`；本地同步使用 `scripts/sync_reports.py`，仅传输轻量报告与日志。下载 DCP 需要用户明确提出。
- 所有实验使用唯一 ID，保留 manifest、status、config、日志和报告。运行成功、时序通过、DRC 检查和位置复用率分别记录，不以一个退出码替代全部结论。
- 2026-09-30 用户明确要求：AMF3 常规 `full-run` 沿用原版允许 Vivado 后端修复的策略，默认 `import_policy=repair`，直接进入修复及布线，不再要求先做严格导入失败轮次。`--strict-import` 和 `--import-only` 保留为可选严格诊断；`--allow-import-repair` 保留兼容。两种模式都记录导入差异、最终位置保留率、完整路由、DRC 与时序，修复结果不改报为严格导入通过。完整导出覆盖和实际约束检查仍保留；历史实验与源码快照不改写。该决定覆盖旧文档中“full-run 默认 strict／repair 仅作诊断”的常规流程约定。
- 无变化、参数变化、连接变化、增删单元、约束变化和缓存不兼容应分别测试。保持冻结对象和资源占用的一致性；不得将数组编号相同当作跨版本对象匹配。
- 速度对比须针对同一个 V1 设计与一致工具/参数条件，区分 AMF 阶段加速和端到端加速。
- 2026-09-28 用户明确要求：AMF 实际布局运行与 Vivado 文件格式适配分别统计，适配时间不计入 placement。DCP→AMF 导出、AMF→Vivado 导出/转换/导入单列；Vivado place_design、route_design、审计/报告/DCP 写出也各自记录。保留 AMF 进程墙钟与端到端总墙钟用于核对，不把进程总时间称为纯算法时间。历史未完整分段的部分标记未单独计时；复用输入缓存说明本轮未执行导出，不伪造首次导出成本。详见 docs/experiment-policy.md。
- 本次初始化不实现增量算法。既有未提交源代码修改已作为基线保存，不得误记为本次新算法成果。
- 所有本项目文件放在项目内，遵循 `docs/workspace-layout.md`。Python/Tcl 文件使用 snake_case，新增文档和普通目录使用小写 kebab-case；历史实验 ID 和上游源码命名保持稳定。
- 单次诊断脚本放在 `scripts/diagnostics/`，预检运行放在 `experiments/preflight/`，诊断证据放在 `experiments/evidence/`。旧脚本仅存于 `archives/legacy-workspace/`，不得当作当前入口。
- 时序目标以用户提供给 placer 的约束为准；当前入口采用配置中的 `ClockPeriod` 及可选 `ClockPeriod:<driver>`。网表连接不能独立给出目标周期，DCP 约束提取为可选辅助，不能自动覆盖用户配置。不把“未自动导入 DCP/XDC”单独作为第一步未完成的依据。目标约束、估计延迟及 Vivado 后端约束分别记录。
- 2026-09-25 用户确认 PDF 第一步“网表输入处理与初始分析”按当前支持范围完成。通用外部宏格式、独立分析入口与全面输入诊断保留为工程完善项，不再据此阻塞第二步。第二步仍为部分完成：AMF 已有 2.0 聚类、PaToH 划分和模拟退火基础；用户独立 `non_dataflow_case` 项目已有资源感知划分、多 SLR 区域规划及软 Pblock，但尚未接入 AMF。不得把 AMF 仓库缺口误记为用户未做过对应工作；优先复用该前端。详见 `docs/research/step-02-topology-floorplanning-assessment.md` 和 `docs/research/partition-floorplan-amf-integration-assessment.md`。
- 2026-09-26 用户收缩当前移植范围：只修改 AMF，以 U250 和原始 GETRF post-opt DCP 为目标；暂不接入其 partition/floorplan、membership 或软 Pblock 结果，暂不实现 SLL 优化。该决定覆盖上条在当前阶段“优先复用前端”的实施顺序，独立前端能力仍保留在既有成果中。需要导入真实 U250 器件数据并补充 URAM 支持；跨 SLR 合法性根据专用级联端口连接判断，不能笼统禁止普通分组或独立 RAM/DSP 跨 SLR。范围与输入预检见 `docs/research/u250-getrf-porting-scope.md`。
- U250 第一阶段已实现并验证器件/输入模型，详见 `docs/research/u250-input-adaptation.md`。`configs/experiments/getrf-u250-input.json` 只用于 `amf3.py inspect`；第二阶段已加入独立 URAM 资源分配、硬资源合法化和 Carry/DSP 专用级联 SLR 检查，见 `docs/research/u250-resource-legalization.md`。`amf3.py legalize-resources` 仅执行硬资源阶段，完整布局入口仍有显式保护。原始划分清单省略时钟负载，必须使用从同一 DCP 补齐时钟后的 `data/reference/getrf-u250/amf-inputs/`，不能把旧 `getrf.zip` 当作完整输入。

- GETRF 全流程入口为 `amf3.py full-run`，配置 `getrf-u250-full.json` 显式开启实验性多 SLR。MUX/SRL、CLB 试插入计数、无固定 I/O 初始化和分数列溢出修复已实现；完整基线 `getrf-u250-full-20260926-183758-400040` 已通过全量布线、DRC 错误/严重警告为 0 和级联审计，10 ns setup 时序未收敛；指标与限制见 `docs/research/u250-getrf-full-flow.md`。稳定构建入口为 `builds/validated-getrf-u250-full/AMFPlacer`。覆盖检查不完整时不得进入正式后端验收，不能把 Vivado 补放遗漏单元记作 AMF 的完整输出。当前标准配置保留 `GlobalPlacementIteration=30` 和默认宏合法化模式；9 次前期迭代曾导致 QP 发散，`DirectMacroLegalize=true` 曾不收敛，失败轮次保留供诊断。

- 2026-09-27 用户要求为现有延迟修正补充纵向 SLR 边界惩罚，覆盖上一阶段“暂不实现跨界时序代价”的限制。参数 `SLRBoundaryDelayNs` 默认 1.5 ns/道边界，GETRF 全流程配置显式设置为 1.5；设为 0 可复现旧延迟模型。原 X 方向修正保留，普通时钟区域的 Y 边界不增加惩罚；SLR 边界从器件元数据推导。仍不加入 SLL 容量或估计拥塞、不接入外部 floorplan。该系数是启发式初值，不代表已校准或新一轮 GETRF 布线后的时序结论。实现、测试与构建见 `docs/research/u250-slr-timing-penalty.md`。

- 2026-09-27 用户批准物理边界 A–D 方案。新模式从 Vivado 全器件 site/tile 与统一坐标映射生成模型；U250 已识别三条 SLR 接缝和内部 HPIO 带，形成 8 个区域。`PhysicalBoundaryMode=true` 替换固定 X 列经验项；`BoundaryAwareClustering=true` 要求新模式，并使用容量预留和 X/Y 软目标。局部硬 IP/未知类型只报告，不把包围盒视为必经障碍。架构规则和系数仍为启发式，不包含 SLL 容量/拥塞。新模式完整 GETRF 对照运行中，不能宣称优于旧模式或替换默认验证基线；实施、构建、测试、比较 ID 见 `docs/research/device-physical-boundary-implementation.md`。

- 上条 A–D 已完成完整验收，最终状态以 `docs/research/device-physical-boundary-getrf-validation.md` 为准。三组均全量布通、DRC/级联合法；本例仅延迟模式 WNS +0.003 ns，二维聚拢模式 −0.059 ns，control −0.137 ns。优先显式使用 `getrf-u250-physical-delay.json` 配合冻结构建 `build-20260927-063221-904893-7a7b5d8b`，保留其中共同数值保护；二维聚拢继续实验，不自动修改全局默认或旧验证构建。OOC 时钟来源警告仍存在，不能将本例 3 ps 余量扩大解释为跨设计或板级稳健收敛。新运行与最终 DCP 均按服务器独立实验目录管理；三组比较和最终哈希证据见 `experiments/evidence/device-physical-getrf-20260927/manifest.json`。

- 2026-09-28 完成 U250/GETRF 的一轮 SA 横纵距离比例数据标定：新配置 `getrf-u250-sa-calibrated.json` 显式使用 `Simulated Annealing y2xRatio=0.71`（有效值，不再乘 0.8），共享 `y2xRatio=0.4`。省略新键时沿用原 SA 计算。正式构建 `build-20260928-002048-349585-d534237b`，报告见 `docs/research/u250-sa-ratio-calibration.md` 和运行 `u250-sa-ratio-calibration-20260928-005532`。1,923 条 routed 连接、保留数据与另一布局验证支持优于旧 0.32 的粗粒度线性代理；不同布局重拟合约 0.55–0.72，不能当作全器件物理常数。本轮未重跑完整 GETRF 布局布线，旧配置仍作对照，不宣称最终 WNS 改善。

- 2026-09-28 随后完成 0.71 的 10 ns 完整实验 `getrf-u250-full-20260928-014049-967020`。与历史严格基线 `getrf-u250-full-20260927-125004-730657`（有效 0.32）相比，总墙钟 147.89→123.88 分钟，AMF 68.57→51.16 分钟，但 WNS +0.111→−0.353 ns、TNS 0→−2.896 ns，26 个 setup 端点违例；hold、全量路由、DRC、级联和 100% 原始 LOC/BEL 保留均通过。最差路径在 SLR 1/2 间跨界五次。0.71 继续作为实验值，不将距离拟合改善表述为端到端 QoR 改善，保留历史 0.32 时序基线。两次构建还存在既有 CLB 打包稳健性差异（本轮未触发列兜底），且服务器负载未严格控制，因此不是纯单变量因果实验。最终 DCP 留在服务器，报告见 `docs/research/u250-sa-ratio-10ns-validation.md`。

- 2026-09-29 用户结束新匹配求解器加速实验，常规使用 `BipartiteMatchingBackend=legacy`；保留 `MacroCandidateSelection=cached_topk` 和只读列表引用优化。五处源码缺省值及常规 GETRF 配置已恢复 legacy。新内核、forward-bias、fast 配置仅供明确选择的历史复现，不自动重新启用。旧内核本身保留原正向 +0.01／反向不抵消；不要再给 legacy 配置新内核的非零 forward-bias。已验证组合及启动命令见 `docs/research/placement-acceleration-inventory.md`，稳定配置为 `configs/experiments/getrf-u250-placement-stable.json`。

- 2026-10-08 发布范围更新：本分支已接入 initialization-only 外部 floorplan，并包含单向分阶段 SLR/HPIO 聚拢与时钟容量表修复；覆盖前文历史阶段的“暂不接入 floorplan”限制。外部区域不永久锁定，未实现 SLL 容量优化和完整时钟布线分配。详见 docs/research/version-diff-20261008.md。历史报告按原日期理解，不改写历史结果。

- 2026-10-12 SRL 兼容发布：根目录算法源码为独立 SLR 最多两轮版，主配置 configs/experiments/getrf-amf3pro-srl-share4-10ns.json，弱牵引开、ProPrimitiveTiming=false、SRLMaxPerSLICEM=4、无外部 floorplan。合并代码保存在 variants/merged/src/，按版本说明在独立副本覆盖构建；不能仅换 JSON 复用主二进制。MCLB 已恢复与 CARRY 耦合重算，历史 own-distance 配置不作为当前入口。此条覆盖历史默认配置推荐；原结果按各自日期解读。两个源码快照及输入哈希见 docs/provenance/amf3pro-srl-compatible-20261012.json。多核并行改造尚未实施。

- 2026-10-20 配置管理约定：后续新实验以 `configs/placement/placer.json` 为常用参数入口，通过 `scripts/export_placement_config.py` 解析层级并生成本轮完整配置，保留历史配置。普通 GETRF 已登记在 `scripts/launch_placement.py`；显式 `--start` 才派发，每轮冻结来源与参数。`visualization=true` 强制 099，可由本地入口 `--config-host 072` 读取 072 配置后跨机派发；不修改 SSH 认证。当前两台二进制能力不同，按 `configs/placement/hosts.json` 核验；DFX 必须使用匹配的静态壳/缓存/模块启动器，不能误用普通 full-run。详见 `configs/placement/README.md`。
