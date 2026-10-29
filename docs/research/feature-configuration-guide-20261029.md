# AMFplacer3.0 功能配置与依赖

适用于新副本 `codex/feature-config-decoupling-20261029`。原合并分支和历史实验不修改。本表列出本次讨论的功能开关及其配套参数；器件输入路径、增量缓存身份等运行环境字段继续来自匹配的基础配置。

未在常用示例中列出的配套参数，在完整基础配置中设置并由导出器继承。配置采用 AMF 的字符串形式，例如 `"true"`、`"0.75"`。示例为 `configs/placement/placer-integrated.json`，通过 `scripts/export_placement_config.py --profile integrated` 与完整基础配置合成。生成的 `resolution.json` 记录实际配置、派生项和失效项。旧 profile 保留，不会把新开关发给不支持它的旧二进制。

## 功能开关

表中的“示例值”属于新配置示例，不表示所有历史版本的缺省值。

| Variable | Options | 示例值 | 作用及依赖 |
|---|---|---|---|
| `IncrementalCompilation` | `true / false` | `false` | 启用增量/DFX 上下文、静态冻结和动态区域资源限制。独立于所有 SLL 开关；DFX 全量也可以使用此上下文。关闭时应换用普通全量的基础配置，不能删除静态保护后继续使用增量缓存。 |
| `SllQuantityEnable` | `true / false` | `true` | **① 数量目标**：近似跨边界需求 × 等效 SLL 长度，以一阶项加入 QP。推动真实 PU 减少跨 die，独立于②。 |
| `SllDynamicTauEnable` | `true / false` | `true` | ①的子开关。开启后按策略变化 τ；关闭后固定使用 `SllQuantityTau`。①关闭时，新配置解析器将此项置为关闭。 |
| `SllPlanningEnable` | `true / false` | `true` | **② 位置规划**：生成成对的入口/出口虚拟位置，参与 die 内分段超边的线长 QP；每轮根据当前跨 die 拓扑创建、保留或删除。关闭后不创建 planner/虚拟变量。 |
| `SllCapacityOptimizationEnable` | `true / false` | `true` | ②的子开关。控制初始化容量评分、密度目标锚点及列利用率软阈值。关闭仍保留合法 tile 投影、物理通道容量和静态资源预留。 |
| `SllTimingCorrectionEnable` | `true / false` | `true` | **③ 跨 die 时序修正，依赖②**：使用该接收端的当前规划树，die 内多项式延迟 + SLL 工艺延迟 + 可选出口长距离修正。关闭恢复普通延迟模型；不关闭全局时序驱动。 |
| `SllRouteConstraintEnable` | `true / false` | `true` | **④ 约束生成与导入，依赖②**：导出接收端分组树并在 Vivado 后端应用、审计。关闭不生成/导入新 SLL 约束，仍保留最终 SLL 通道合法化。旧文件的存在不会触发导入。 |
| `DieFirstSpreadingEnable` | `true / false` | `true` | 独立控制优先在同一 die 内扩散；局部容量/搜索条件不足时允许按现有策略跨 die。关闭使用普通扩散，不关闭 SLL 目标。 |
| `TimingDrivenPlacementEnable` | `true / false` | `true` | 全局时序驱动：QP 时序加权/伪网、时序宏匹配偏好、打包关键性评分和时序详细优化。关闭保留线长、容量、合法化及 STA 报告；③仍可单独改变报告的延迟模型。显式聚类开关单独控制。 |
| `DetailedDiagnosticsEnable` | `true / false` | `false` | 控制详细 QP/pin/net 跟踪、SLL 规划与缓存诊断、宏位置快照及扩散细节。关闭不改变优化公式，保留必要结果、τ轮次记录、合法性/静态资源/后端验收审计。坐标可视化由 `visualization` 独立控制。 |
| `IterativeMacroLegalizationEnable` | `true / false` | `true` | 只控制 GP 迭代中的宏匹配和对应伪网牵引。关闭后按配置轮次结束 GP，打包前仍执行最终硬宏精确合法化；最终 CLB site/BEL 和 SLL 通道合法化保留。 |
| `visualization` | `true / false` | `false` | 独立输出坐标可视化轨迹，不控制优化；沿用工作区可视化实验的主机调度约定。 |

依赖关系：

```text
SllQuantityEnable (①)
└─ SllDynamicTauEnable → SllTauPolicy
SllPlanningEnable (②)
├─ SllCapacityOptimizationEnable
├─ SllTimingCorrectionEnable (③)
│  └─ SllOutputLongDelayCorrection
└─ SllRouteConstraintEnable (④)
```

①和②可同时开启：①优化“是否以及跨多少道边界”，②优化“在哪里跨、各 die 内如何连接”。②没有包含①的数量梯度，因此常规 E2 同时开启；做消融时可分别关闭。③或④显式开启而②关闭，会直接报配置错误。其他子开关在父开关关闭时不参与相应优化。

## τ 与数量模型

| Variable | Options / Range | 说明 |
|---|---|---|
| `SllTauPolicy` | `adaptive / periodic / drop-or-timeout` | 保留 E1 三种实现，见下表。只在动态 τ 开启时生效。 |
| `SllQuantityTau` | positive number | 固定 τ 值，示例 `10`。动态模式从 `10` 起步，同一轮的两次 QP 使用相同 τ。 |
| `SllQuantityGamma` | positive number | WA 聚合平滑参数，示例 `0.02`；与 τ 作用不同。 |
| `SllTauCompletionRequired` | `true / false` | 仅对动态 `periodic` 生效。`true` 保留原周期策略的完成条件：至少30个完整轮次，τ=0.2 至少保持4轮；`false` 允许按普通 GP 条件结束。不是所有 τ 策略都强制跑到下限。 |
| `SllShortSpanEnable` | `true / false` | ①的实验子项。暂时排除靠近同一道边界且跨度很小的网的数量梯度；进入阈值2、恢复阈值3，离散需求统计仍包含这些网。当前示例不额外开启。 |
| `SllRegionPolicy` | `preserve / release-single` | **独立于①和②**。`preserve` 保留已有区域偏好，当前采用。`release-single` 为已实现的实验策略，当前暂不启用：删除未固定/未锁定、符合条件的单体 LUT/FF PU 的整条偏好，同时取消该偏好的锚点牵引和越界裁侧；不解除 DFX/Pblock 硬限制。 |

| `SllTauPolicy` | 更新规则 |
|---|---|
| `adaptive` | 原动态 τ。初始6轮、每档至少4轮、换阶段至少稳定2轮；结合 S 的三轮中位数、10%下降或平台条件，回升时暂缓。逐次减半，最低0.2。 |
| `periodic` | 不按 S 决策。前6轮为10，此后每4轮减半：5、2.5、1.25、0.625、0.3125、0.2。 |
| `drop-or-timeout` | S 相对参考下降10%，或达到等待上限即降档。首档至少6轮、最多14轮；中间档最多4轮，0.3125档最多3轮；最低0.2。沿用自然退出。 |

## 位置、容量、时序及约束配套项

| Variable | Options / Range | 说明 |
|---|---|---|
| `SllJointPlacementEnable` | `true / false` | 兼容底层字段；新 profile 由②派生。②开启时为 `true`，不要在新 profile 中另行关闭。历史非 joint 实验仍可用原完整配置复现。 |
| `SllTilePlanningEnable` | `true / false` | 兼容/能力校验字段，新 profile 随②设置，当前联合规划按 tile 粒度实现。 |
| `SllPlanningAllowSplit` | `true / false` | 历史实验字段。新 profile 固定 `false`：同一 net 同一道边界使用一个共享 crossing，不按远端扇出拆分。 |
| `SllColumnCapacityEnable` | `true / false` | 底层列容量字段，新 profile 随 `SllCapacityOptimizationEnable` 派生。 |
| `SllColumnTargetUtilization` | `(0, 1]` | 整列首选阈值，示例 `0.75`。按满足当前阈值的候选列选择，不是等到列100%满才扩展。 |
| `SllColumnMaxUtilization` | `[target, 1]` | 整列放宽上限，示例 `0.85`。不是 tile 阈值。实验验收的例外阈值不能当作这个规划参数的实际值。 |
| `SllColumnUtilizationStep` | `(0, 1]` | 整列阈值放宽步长，示例 `0.01`。 |
| `SllInitialColumnUtilizationLimit` | `(0, 1]` | 初始化阶段的发送端邻近列筛选参数，通常 `0.75`；开启统一列容量策略时以该策略为准。 |
| `SllDensityTargetUtilization` | `(0, 1]` | tile 密度目标，示例 `0.80`。U250 当前资源表中一个 tile 为24条规范化 SLL 通道。 |
| `SllDensityRowHeight` | positive number | 密度分桶 Y 粒度，当前默认 `1`，对应现有映射中的一个 tile 行；不是360根线的桶。 |
| `SllDensityWeight` | nonnegative number | 软容量代价系数，现有默认 `256`。容量总开关关闭时有效值为0。 |
| `SllDensityAnchorWeight` | nonnegative number | 密度目标到 QP 的锚点强度，现有默认 `0.05`，底层保留数值下限。容量总开关关闭时完全不添加该锚点。 |
| `SllPlanningAllowTransientOverflow` | `true / false` | 允许布局中临时溢出，最终仍须合法化；依赖 joint 规划与列容量策略。关闭容量总开关时此能力也关闭。 |
| `SllInitializationPolicy` | `local-wire-capacity` | 新 crossing 初始化只看两侧局部超边线长及容量；X优先发送端附近，Y同分按利用率、原位置/区间中部处理。 |
| `SllProjectionPolicy` | `qp-nearest-capacity` | 求解后以虚拟 cell 的 QP 结果投影到合法 tile，而非重新按初始化评分选列。 |
| `SllQpDisplacementPolicy` | `shared-pre-projection` | 先执行与真实 PU 相同的 QP 位移限制，再做合法 tile 投影；不限制投影后的换列距离。 |
| `SllOutputLongDelayCorrection` | `true / false` | ③的子项：出口侧长距离修正。普通中短距离公式保留。 |
| `SllOutputLongDelayThresholdNs` | positive number | 出口段基础估计延迟阈值，既有标定值 `1.5` ns。 |
| `SllOutputLongDelaySlope` | nonnegative number | 既有标定值 `1.1430759607609453`；修正为 `d + slope × max(0, d − threshold)`，其中 d 是出口侧原模型的延迟。 |
| `SLRBoundaryDelayNs` | nonnegative number | 普通模型的粗略每边界罚时，既有值 `1.5` ns。③开启且有明确规划路径时被分段模型替换，不重复叠加；没有规划的连接仍用普通模型。 |
| `SllConstraintWorkers` | integer `1..8` | ④的 Vivado 并行约束生成/审计工作进程数，示例4；AMF 的并行导出仍使用 `jobs`。 |
| `SllRefreshParallelEnable` | `true / false` | 规划拓扑收集的并行实现开关，默认 `true`，用于性能复查。 |
| `SllWireCacheEnable` | `true / false` | 候选线长查询缓存，默认 `true`，用于性能复查。 |
| `SllQuantityDiagnostics` | `true / false` | 历史数量模型诊断请求，不等于①。详细输出还受 `DetailedDiagnosticsEnable` 控制。 |

时序修正不再通过独立候选 SLL 搜索替没有身份的坐标对另选 crossing；有源/接收端身份且受规划覆盖时读取当前树，否则使用普通模型。SLL 坐标映射、工艺表和静态预留输入仍需匹配：`SllChannelFile`、`SllCoordinateMapSha256`、可选 `SllChannelFileSha256`；增量由流程生成 `SllIncrementalScope=internal-active-nets` 与 `SllReservedResourceFile`。

## 聚类、区域偏好与普通参数

| Variable | Options | 说明 |
|---|---|---|
| `BoundaryAwareClustering` | `true / false` | 开启新的物理边界感知聚类；关闭后是否做旧 CR 聚类，由 `noBlockAware` 决定。不是所有聚类的总开关。 |
| `BoundaryClusteringStrategy` | `slr-hierarchical / cr-slr-hierarchical` | 前者按 SLR 层级形成区域目标；后者使用跨 SLR 接缝附近的 CR 行目标，并结合横向边界的 CR 列目标。仅新聚类有效。 |
| `BoundaryStageMode` | `independent-max2 / merged` | 分开的 SLR 引导阶段最多2轮，或合并阶段。结束牵引不等于解除仍有效的区域边界。 |
| `BoundaryRegionClipping` | `true / false` | 新聚类区域偏好的越界裁侧。和 DFX 硬合法区不是同一个约束。 |
| `BoundaryAnchorStopOnRegionEntry` | `true / false` | 进入目标区域后是否停止对应区域锚点牵引。 |
| `BoundaryWeakAttraction` | `true / false` | 对适用路径使用弱区域/CR吸引；部分旧 U250 列引导路径不使用此系数，解析报告会标出。 |
| `noBlockAware` | `true / false` | 名字含否定：`false` 允许旧 CR 聚类，`true` 关闭旧 CR 聚类。新边界聚类开启时此项不参与选择。 |
| `ClockRegionColumnStretch` | `true / false` | 旧 CR 聚类的列拉伸步骤；新边界聚类路径不执行它。 |
| `ProFFClustering` | `true / false` | Pro 的 FF 聚类独立开关；不等于新边界聚类。 |
| `ProPrimitiveTiming` | `true / false` | 普通 Pro 基线使用 `false`；E2 当前也要求 `false`。 |
| `ProLegacyIOBoundary` | `true / false` | 旧 VU095/VCU108 I/O 边界延迟特例；受器件和物理边界模式兼容检查限制。 |
| `PhysicalBoundaryMode` | `true / false` | 物理边界模型开关，新边界聚类会派生为 `true`。SLL 规划/数量模型也需要相应器件边界元数据。 |
| `device` | `U250 / VCU108 / VU095 / VHK158` | `VU095` 归一到 `VCU108`；`VHK158` 当前保留但不支持运行。 |
| `ClockPeriod` | positive number, ns | AMF 目标周期。逐驱动覆盖为 `ClockPeriod:<driver>`。导出配置不会自动更改 Vivado XDC。 |
| `jobs` | positive integer | AMF 并行线程数；不是 Vivado 约束工作进程数。 |
| `GlobalPlacementIteration` | positive integer | 普通 GP 阶段预算的基础参数，来自基础配置；关闭迭代宏合法化仍保留阶段预算。周期 τ 的完成条件可能延长最后阶段。 |

PU 区域偏好由对应聚类/区域分配流程生成，记录在 `regionPreferences`；`SllRegionPolicy` 决定是否保留其中符合条件的单体 PU 偏好，不是根据 net 是否跨 die 临时猜测所属区域。旧 CR 列中心表、FF 聚类和 DFX 硬区域不由它统一删除。

## 使用与验证

```bash
python3 scripts/export_placement_config.py \
  --config configs/placement/placer-integrated.json \
  --base /absolute/path/to/matching-amf-config.json \
  --profile integrated --output /absolute/path/to/new-config-directory
```

该命令只生成配置，不启动实验。增量实验继续使用原 DFX 启动器和匹配的静态壳/缓存；只开启②而关闭④时，仍须扣除静态壳资源并为并行 RM 分配互斥 SLL 资源。

验证完成：独立构建成功；52个回归阶段全部通过，包含133项 Python 测试、180轮 τ 策略对照、U250 四单元 QP/STA/约束导出开关测试，以及既有增量静态缓存/资源隔离测试。配置导出示例已实际生成完整配置。

关闭迭代宏合法化的四 FF 小样例按1轮预算退出并进入最终合法化；它不包含大型 DSP/RAM 组合，不能替代大型设计的性能和 QoR 对照。前两次扩展夹具缺少 PU 连网统计和 SRL 元数据配置，失败记录已保留；补齐后整套回归重新通过。

服务器源码：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261029-feature-config-decoupling/repo`。验证记录：该副本下 `experiments/evidence/feature-config-verified/manifest.json`。构建为 `builds/build-20261029-222128-358064-bf7ee33d/build/AMFPlacer`，源码来自原合并提交 `bf7ee33d` 加本次记录的修改，具体文件哈希保存在构建目录。

本次未重新运行 GETRF 布线；开关验证不代表解决既有 DFX 接口 SLL STUB/计划偏离问题。
