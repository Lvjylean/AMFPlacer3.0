# 现有 partition / floorplan 项目与 AMF 接入评估

评估日期：2026-09-25。本次为源码、版本和既有实验记录审查，没有修改算法、启动实验或验证 AMF 多芯粒运行。

## 结论与范围修正

**可以接入，但需要开发接口和器件支持，不能只传入现有 Pblock Tcl 或修改配置。最合适的分工是：现有项目提供资源感知 partition 与多 SLR 区域规划，AMF 提供受区域偏好引导的 cell 级布局，Vivado 完成后端实现与时序验证。**

现有独立项目与计划书第二步、尤其（2a）“多芯粒资源估计与拓扑结构预排布优化”高度重合，已经有可以复用的主体工程。此前 [第二步评估](step-02-topology-floorplanning-assessment.md) 中的缺口仅描述 AMFplacer3.0 仓库，不能解读为用户尚未实现这部分工作。独立项目已有能力、AMF 集成完成、完整计划书验收是三个不同状态。

## 核对版本

- AMF：`eda072:/Projects/jinyang/workspace/AMFplacer3.0`，提交 `314c6c75a7f21031ffcbd750fd2dde7a77b3cfb8`；检查时工作区干净。
- 用户仓库：<https://github.com/Lvjylean/non_dataflow_case>。`main` 为 `ae4b3667baf85a03ebc99db397d4db762b06ccbc`，不能仅看 main 判断当前能力。
- 已核对发布分支 `partition-floorplan-9_25_删除D/WK/保护子算子` 指向 `887ca564b1f679c840b1c4f417665cbaa4d3dbac`。
- 当前本地源码根目录：`/Users/jinyanglyu/Documents/ChatGPT/New project/workareas/nondataflow-partition-v2`。其 Git HEAD 较旧，但存在当前源码覆盖及未跟踪文件，本次未改变这些文件或 Git 状态。
- `FPGA_benchmark/netlist_hier_floorplan/connection_partition/release_9_25_manifest.json` 的 101 个源码条目与当前本地文件比较：98 个一致，3 个变化（README、structure_floorplan.py、structure_pipeline.py），无缺失。之后的可选链区域引导和控制路径修整按本地新增能力评估，不当作 9/25 发布快照已包含的内容。

下文用户项目路径均相对此本地根目录；`C` 指 `FPGA_benchmark/netlist_hier_floorplan/connection_partition/`。AMF 路径相对服务器 AMF 根目录。

## 用户项目实际上已经完成的工作

1. **资源驱动的模块生成。** `C/resource_partition.py` 从 CR 表按 SLR 汇总多类型资源；每个模块必须存在同一个芯粒容量向量能容纳其全部资源需求，而非从不同芯粒各取一个资源最大值。该条件是模块粒度可行性，尚不是所有模块同时放置可行性的证明，也不强制最终模块只占一个 SLR。
2. **超图分割与结构保护。** KaHyPar 递归二分、选择性合并和局部修整；partition 目标为 `J=C+L`，C 使用加权 km1，L 为逻辑核心分离代价。算子和经过验证的存储—计算联合链受保护。当前 partition 已去掉几何 D 和较少模块数的偏好，不应再用旧版目标描述它。
3. **多 SLR / CR 级规划。** Gurobi 选择初始区域，逐 CR 检查多资源叠加约束；之后有移动、交换、调整区域和模拟退火搜索。候选支持跨 SLR 区域，module、物理区域组与 die 不是一一对应关系。
4. **跨 SLR 通信代理代价。** 有向连接、空间距离、SLR 距离及跨多 SLR 分支惩罚已参与 floorplan。它们是拓扑/几何代理值，不能直接当作纳秒延迟或实际 SLL 占用。
5. **可交付的软区域约束。** `membership.tsv`、`core_solution.json`、`edge_padding.json`、`regions.tcl`、manifest 和 DCP 哈希组成实现输入。核心区域用于容量与目标计算，软扩展只改变引导几何，不虚增资源容量；精确 CR 集合不能粗略替换成包围矩形。
6. **较新的可选修整。** 链区域引导保留逻辑模块，生成成员互斥的物理区域组；控制路径修整利用可严格映射的 Vivado 时序报告，减少同一周期内反复跨 SLR 的路径。这些是局部反馈机制，不是完整、经校准的多芯粒 STA。

既有证据：`FPGA_benchmark/reports/getrf_auto_joint_20260925_01/` 记录 912,247 leaf cells、10 个逻辑模块及 256 条联合链的 partition 检查；`getrf_joint_region_guidance_20260925_01/` 进一步导出 12 个物理组。后者本轮只做静态检查，原生 Vivado 绑定和新一轮 P&R 尚未完成；不能将代理成本改善记为新时序改善。其他历史物理实验不因此被否定。

## AMF 能否约束一群 cells

**AMF 已有群组聚集机制，但没有用户指定 module 区域的完整接口。**

| 现有机制 | 源码证据 | 能做什么 / 缺什么 |
|---|---|---|
| `designCluster` 外部 cell 群输入 | `designInfo/DesignInfo.cc:485`（完整前缀 `src/lib/HiFPlacer/`） | 读取按精确 cell 名称列出的群组；不读取 SLR 或区域几何 |
| 群内网络增强 | 同文件 `:534–577` | BRAM/DSP 群及大小不在 16–200 的群跳过该增强；其余按连接条件增强。该限制只针对这条机制，不能推断大群完全无效 |
| 群重心伪网络 | `placement/globalPlacement/WirelengthOptimizer.cc:943` | 对满足条件且至少 24 cells 的群，把 PU 拉向当前加权重心；权重随迭代衰减，大群有 `200/N` 缩放。不是用户指定区域 |
| 外部群驱动初始划分函数 | `placement/globalPlacement/ClusterPlacer.cc:257,349` | 存在相关函数，但默认调用链没有用它取代内部初始聚类；不能认为传入文件后就保留用户 partition |
| 时钟区域锚点伪网络 | `placement/globalPlacement/WirelengthOptimizer.cc:1031` | 现有专用启发式主要处理 X 方向，相关 Y 项被注释；映射还会被内部阶段清空，不是通用且持久的二维区域约束 |

上述伪网络会由实际全局布局调用，因此可以复用其数值优化基础。需要新增稳定的 `cell/PU → group → preferred region` 数据结构、区域距离目标及阶段传递，而不是把用户模块变成 AMF 的刚性物理宏。AMF 的 carry / cascade / RAM 宏可能有相对位置和合法性要求；逻辑模块及联合链的保护关系不自动意味着固定形状。

用户 Tcl 的具体语义也要保留：`FPGA_benchmark/netlist_hier_floorplan/balanced_implementation/adapter.tcl:35–50` 按 leaf cell 名称创建 Pblock 并设置 `IS_SOFT true`。AMD 文档说明，软 Pblock 用于初始布局，后续优化可以让 cells 离开区域以改善时序。因此 AMF 中的软偏好也应允许受控偏离，并报告最终区域保持率；不能一方面称“软”，另一方面全程硬钳制。

官方语义来源：[Vivado 2023.2 UG912 IS_SOFT](https://docs.amd.com/r/2023.2-English/ug912-vivado-properties/IS_SOFT)。

## 多芯粒接入的实际工作量

当前 AMF 的设备和容量主要围绕 Site/BEL/Tile/ClockRegion 及二维坐标展开，缺少供优化器使用的显式 SLR 归属、跨 SLR 互连容量和延迟模型。二维坐标能覆盖整片器件，不等于优化器已经理解芯粒边界。

还有比区域接口更早的具体障碍：当前 GETRF 规划包含 URAM，而 AMF `src/lib/HiFPlacer/designInfo/DesignInfo.h:60–134` 的原语类型表没有 URAM，`DesignInfo.cc:605–617` 对未知类型报错并断言。因此未经转换和支持扩展，不能把该设计原语直接交给当前 AMF。正式支持还需要资源/site/BEL 描述、打包/合法化、位置导出及必要的时序模型，不能只增加枚举或把 URAM 当 BRAM。

最低改造范围包括：

- 目标器件原语支持清单及器件导出，资源站点、CR 到 SLR 的映射。
- 每 SLR 的资源、固定对象、时钟与强制物理宏可放置性；跨边界合法条件。
- 与实际 SLR 边界对应的连接代价，之后再校准跨界延迟和互连容量；网络数量不等于 SLL 通道使用量。
- 模块软区域在初始布局、全局优化、扩散、打包/合法化之间的传递和放松策略。
- Vivado 回读后的区域保持、实际跨 SLR 情况、路由与时序验证。

优先考虑保持一个全芯片 AMF 实例，再加入明确的芯粒模型。简单把每个 SLR 当独立器件跑多个 AMF，会另外引入跨芯粒端点、共享时钟、全局时序及资源归属协调问题，不能当作现成捷径。

## 与计划书第二步的对应关系

| 计划书能力 | 用户现有工作 | 判断与剩余差距 |
|---|---|---|
| 多芯粒资源估计、资源均衡 | SLR 资源向量、CR 供给、模块需求和联合容量检查 | 主体已有；需接入 AMF，保持静态估计与实际合法布局的区别 |
| 层次化聚类、自适应粒度 | 保护算子/链、资源驱动二分、合并与修整 | 工程高度重合；严格的“先芯粒内、再跨芯粒聚类”流程并不完全相同 |
| 加权超图与拓扑优化 | 加权 km1、逻辑核心代价、多资源可行性 | 主体已有；完整时序/拥塞反馈的自适应权重尚需补充 |
| min-cut 整数规划 | KaHyPar partition + Gurobi 区域分配 | 两类优化均已有，但不能说已经实现计划书指定的 min-cut ILP；需明确采用等效路线还是补该求解器 |
| 多目标预布局、模拟退火 | 跨 SLR 代理代价、资源约束、区域选择与 SA | 主体已有；资源与通信占主要部分，关键路径反馈仍不完整 |
| 芯粒感知路径估计和初步 STA | 有向跨 SLR 成本；较新可选控制路径报告反馈 | 部分已有；不是完整的数值延迟传播、跨芯粒 STA 或 SLL 容量模型 |
| 宏预放置及物理可行性 | 大模块/联合链的区域引导 | 已有粗粒度定位；不等于 AMF 物理宏的 site/BEL 级预放置和合法化 |
| 面向后续 placer 的初始方案 | leaf membership、核心/软扩展区域、哈希与导出 | 接口基础扎实；目前面向 Vivado，需要 AMF adapter |

结论不使用百分比：需求条目共享基础设施，按条数计算比例没有意义。可以把现有项目定位为 AMFplacer3.0 的“多芯粒高层优化前端”；它不是与课题无关的外围工具，也不代表完整增量编译器已经完成。V0/V1 匹配、变化范围、冻结/活动区域和局部重布局仍是后续独立任务。

## 建议接入契约与优化方式

```text
同一个 Vivado post-opt DCP
    ├─ 已有网表/资源导出 → partition → multi-SLR floorplan
    │                                     ↓
    │                       membership + core / preferred regions
    │                                     ↓
    └─ AMF 网表导入 → 区域适配器 → cell 级布局 → Vivado 后端验证
```

优先复用现有 JSON/TSV 和输入哈希，不让 AMF 执行 Tcl 来猜测约束。共享的中间契约至少记录：DCP 哈希和处理阶段、目标 part、精确 leaf 名称、逻辑模块 ID、物理组 ID、核心 CR 集合、软偏好 CR 集合、资源容量模型、约束强度和生效阶段。10 个逻辑模块变成 12 个物理组时，两级身份都要保留。

CR 坐标需要转换成 AMF 的实际位置/资源站点坐标；非矩形 CR 并集需要原样表达，不能用外包矩形填补空洞。基础设施或固定 cell 使用独立归属策略。AMF 打包成 PU 后，必须检查一个强制物理宏是否跨多个外部群，解决冲突后再加约束，避免同一 PU 重复计权。

建议软约束目标为：`E_region = Σ_m Σ_(u∈m) λ_m(t) × distance(position_u, preferred_region_m)^2`。区域内为零，区域外拉向最近可行边界；对于多个矩形取并集距离。λ 随阶段调整，由密度与资源合法性约束避免聚集。这是建议的新设计，现有代码尚未实现。

不要把几十万 cells 都拉向一个中心点。可以复用 AMF 的伪网络构造，在每次外层迭代为区域外 PU 更新投影锚点；同时保留硬资源合法性和拥塞优化。用户软扩展不增加实际容量。外部计划启用时，应增加模式以采用该初始规划，避免默认内部聚类/SA 覆盖它；模块内部仍可继续细化。

## 实施顺序与验收

1. **先验证区域接口。** 在当前 AMF 已支持的器件和小设计上构造有区分度的多个模块/区域，校验精确 cell 映射及 PU 归属；对照无引导/有引导，测量区域保持、偏离、合法性和时序。此步验证接口，不代表多芯粒成功。
2. **并行规划器件移植工作，但不混成同一次调试。** 以现有多 SLR case 做完整原语及站点清单，明确 URAM 等支持缺口，再补充器件、SLR 和跨界模型。复用已有 partition/floorplan，不重新开发同一套模块生成器。
3. **完成真正的多芯粒对照。** 同一 post-opt DCP、同一前端区域计划，比较“现有 floorplan + Vivado”与“现有 floorplan + AMF + Vivado”。固定时序约束和工具参数，记录前端、导出/导入、AMF、Vivado 各阶段及端到端时间，以及 WNS/TNS、路由状态、资源/区域保持和实际跨 SLR 结果。

最终是否提升 QoR 或总编译速度必须实验确定；AMF 单独布局快、代理成本下降、静态容量检查通过，都不能替代上述对照。本次评估不承诺加速比例或实现工期。

## 关键源码与记录索引

- `C/RESOURCE_PARTITION.md`、`C/resource_partition.py:14,54,76,91`：资源、目标、划分。
- `C/balanced_regions.py:16,27`、`C/fixed11_explore.py:28`：多 SLR 候选、MIP 初始化与 SA。
- `C/directed_model.py:44`、`C/EDGE_PADDING.md`：跨界代理成本、容量/软几何分离。
- `C/structure_floorplan.py:152`：实现输入与哈希；`C/CHAIN_GUIDANCE.md`、`C/CONTROL_PATH_REFINEMENT.md`：后续可选能力。
- `FPGA_benchmark/reports/partition_floorplan_publication_20260925_01/README.md`：发布范围。
- `FPGA_benchmark/reports/getrf_joint_region_guidance_20260925_01/README.md`、`getrf_control_paths_20260925_01/README.md`：静态结果及明确的验证边界。

本报告引用既有验证记录；本轮没有重新执行这些测试或 Vivado/AMF 实验。
