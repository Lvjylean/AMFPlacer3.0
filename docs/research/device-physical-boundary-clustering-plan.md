# 器件物理边界识别与二维关键路径聚拢：修改前方案

状态：**待用户审阅，尚未实施**。2026-09-27。代码基点 `b572999ac46095ac56aea851c039bb36ebe7ec09`，服务器 `/Projects/jinyang/workspace/AMFplacer3.0`。本文所有代码路径均相对于该服务器仓库；文件名标注“新增”的尚不存在。

本轮只形成方案文档，不修改算法、构建或实验配置，不启动新的 GETRF 布局布线。

## 1. 本轮目标与范围

从 Vivado 器件对象提取几何结构，用可审计的架构规则识别重要边界，再由统一延迟模型和二维聚拢策略使用这些边界。位置由器件数据决定，不再按板卡名或固定 clock-region 列号判断。

首轮目标为 U250 / UltraScale+ 的已知资源类型，保留 VCU108 旧模式回归。其他器件可以复用采集、解析和查询框架，但未知资源类型和不支持的拓扑必须明确报告，不能自动宣称完全支持。

沿用当前约定：不接入外部 partition/floorplan；不建立 SLL 容量、拥塞或详细路由模型；不修改网表流水级；不改变用户时序约束；最终 DCP 仅存服务器。放置槽位容量检查属于本轮可行性检查，与 SLL 带宽和拥塞模型不同。

## 2. 已核对的事实与当前代码缺口

- 空器件查询已取得 346,675 个 site、40 种 SITE_TYPE、4 个 SLR、128 个 clock region。HPIO 列为 RPM_X=2576，归属 clock-region X4；CONFIG、PCIe、CMAC 等也有可查询位置。这是几何证据，尚不是已校准的布线障碍。
- `export_fabric_device.tcl` 当前只正式导出 SLICE/DSP/BRAM/URAM，缺少完整 I/O、硬 IP 与 tile 范围。
- `prepare_fabric_device.py` 的 X 来自归一化 RPM，Y 来自 clock-region 内资源行；不能直接把原始 RPM_Y 或 tile ROW 当成 AMF 的 Y。
- `PlacementTimingOptimizer.h` 保留原 X 方向列号判断，并新增 1.5 ns/SLR 接缝。新模式必须避免把原 X 修正与物理边界修正重复叠加。
- `clusterLongPathInOneClockRegion()` 按长路径及 X 列统计，目标 Y 保持原值；含 DSP 的候选还会被放弃。单纯增加 Y 吸引点不能完成结构自适应。
- `WirelengthOptimizer::updatePseudoNetForClockRegion()` 仅启用 X 方向吸引；U250 分支已取消部分固定列号，但其他消费者仍使用列目标。
- `GeneralSpreader.cc` 仍有标注 VCU108 的 X 列 1/2/3 硬裁剪；新模式必须绕开并替换。
- `stretchClockRegionColumns()` 会按整列拉伸 Y；其中两个查询误传了 `(PU->X(), PU->X())`。不能把这段原逻辑直接当成新的 SLR 内摊开规则。
- `PlacementInfo::findNeiborSiteFromBinGrid()` 有按目标 X 列过滤候选的入口；新目标不能只写入聚拢器而不更新这些下游接口。

## 3. 修改与新增文件清单

| 层次 | 文件 | 计划修改 |
|---|---|---|
| Vivado 导出 | `scripts/export_fabric_device.tcl` | 增加可选结构导出模式；保留原 fabric 输出，另输出完整 site、tile、SLR/clock-region、bank 关联与元数据。记录工具版本、part、查询缺项与输入范围。 |
| 坐标转换 | `scripts/prepare_fabric_device.py` | 输出可复用的坐标映射描述；保留原 fabric 坐标及旧调用方式。边界导出复用同一映射，禁止混用 RPM、tile 和 AMF 坐标。 |
| 边界生成（新增） | `scripts/build_physical_boundaries.py` | 结合 site、tile、规则与坐标映射生成边界、候选区域、可用资源统计、未分类对象和来源报告。 |
| 架构规则（新增） | `configs/architectures/ultrascale-plus-boundaries.json` | 保存已知 SITE_TYPE/TILE_TYPE 分类、几何合并条件、初始代价和置信等级；不保存 U250 的固定列号。 |
| C++ 器件模型（新增） | `src/lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.h/.cc` | 加载、校验边界数据；提供区域归属、分隔边界计数、局部障碍查询、惩罚查询及空间索引。 |
| C++ 器件接入 | `src/lib/HiFPlacer/deviceInfo/DeviceInfo.h/.cc` | 持有新模型，核对器件与坐标映射；复用现有 SLR 元数据。未启用新模式时保持原行为。 |
| 延迟模型 | `src/lib/HiFPlacer/placement/placementTiming/PlacementTimingOptimizer.h/.cc` | 新模式使用“原距离回归＋物理边界修正”，替代旧固定 X 列修正；统一经过两个 getDelayByModel 接口。 |
| 聚拢器（新增） | `src/lib/HiFPlacer/placement/placementTiming/BoundaryAwareClusterer.h/.cc` | 提取关键逻辑、选择二维候选区域、检查资源、评价受影响连线、建立软目标并输出决策原因。 |
| 资源预算（新增） | `src/lib/HiFPlacer/placement/placementInfo/RegionCapacityTracker.h/.cc` | 维护区域实际占用和本轮聚拢预留；支持试分配、释放和回滚，防止多个簇重复消耗同一批余量。 |
| 目标与候选站点 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h/.cc` | 增加 PU 的区域偏好及预算状态；新模式将旧“只选某列”改为软候选排序和有界回退；打包后清理失效目标。 |
| QP 吸引 | `src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.h/.cc` | 增加区域软吸引：按需作用于 X/Y，目标是最近可行位置，不把整个簇压到一个中心点；沿用现有锚点权重尺度。 |
| 摊开协调 | `src/lib/HiFPlacer/placement/globalPlacement/GeneralSpreader.h/.cc` | 新模式替换 VCU108 固定列裁剪；容量不足允许外移并记录偏离，不将软目标升级为硬 Pblock。 |
| 流程接入 | `src/app/AMFPlacer/AMFPlacer.h` | 在现有三次聚拢位置按模式调用新聚拢器；在 LUT/FF 配对等 PU 变化后清理、重建目标和预算。 |
| 运行与记录 | `scripts/amf3.py`、`scripts/inspect_amf_inputs.py`、`scripts/run_full_flow.py`、`scripts/report_bundle.py` | 接入模型预检，解析新文件路径、校验并保存输入哈希，记录最终生效代价、边界报告和阶段性聚拢指标；确保轻量报告可同步。 |
| 后端测量 | `scripts/full_backend.tcl`、`scripts/summarize_full_flow.py` | 输出受约束关键路径的跨界统计，区分估计值与布线后值；修正漏检 Route 35-197 的问题。时钟来源缺失仅报告，不能虚构 HD.CLK_SRC。 |
| 测量辅助（新增） | `scripts/diagnostics/export_boundary_timing_samples.tcl`、`scripts/diagnostics/analyze_boundary_timing_samples.py` | 从已有 routed DCP 抽样连接延迟与边界类型，生成系数评估报告；不自动覆盖正式参数。 |
| 实验配置（新增） | `configs/experiments/getrf-u250-physical-boundaries.json` | 保存新模式与初始参数，保留现有 GETRF 配置作为对照。 |
| 测试/构建 | `tests/test_physical_boundaries.py`、`tests/check_boundary_clustering.py`、`src/tests/check_physical_boundaries.cc`（新增），`tests/test_full_flow.py`、`src/CMakeLists.txt` 等 | 覆盖离线生成、原生模型、区域预算、软目标、兼容模式与记录契约。新增类遵循所在目录的现有 CMake 源文件收集方式。 |

本轮不重写 `ClusterPlacer`/`SAPlacer` 的整体初始划分与退火代价，也不重写 `MacroLegalizer` 的匹配目标。新模型提供可复用接口，并记录这些阶段前后的变化，便于下一轮接入；不将本轮描述成所有布局阶段都已具备边界优化。

## 4. 数据结构与坐标契约

建议分离三个对象，避免把“不能放 cell”与“连线穿过有代价”混为一谈：

- `PhysicalBoundary`：稳定 ID、类型、方向、有效范围/连续片段、两侧可用区域、来源对象、证据等级、惩罚策略。贯穿边界和局部区域分别表示。
- `PlacementRegion`：由 SLR 与已确认分隔结构形成的可用逻辑区，允许由多个矩形片段组成；记录合法站点集合及资源统计。局部硬 IP 不直接生成贯穿整个 SLR 的切线。
- `RegionPreference`：当前 PU/簇偏好的区域、软吸引强度、预留资源、有效阶段及决策原因。与用户固定位置/合法性约束分开保存。

生成器同时输出便于审阅的 `boundaries.json`/区域图和 C++ 可直接读取的版本化 `physical_structure.tsv`。TSV 含模型头和有类型的边界/区域/站点映射记录；不把嵌套 JSON 强行交给当前只支持简单配置的 `simpleJSON`。正式配置新增 `physical boundary model file` 指向该文件。

模型头必须包含 schema、part、架构规则版本、fabric/device 哈希、坐标映射哈希、原始数据哈希与生成范围。运行入口先核对哈希，C++ 再核对格式、ID、站点归属与坐标一致性；显式启用新模式但数据缺失或不匹配时应报错，不能静默退回。

几何归一化须保持原 AMF fabric 坐标不变。X 使用现有映射；Y 用 clock-region 与 SLICE 行锚点建立分段映射，校验方向、单调性及接缝位置。tile ROW 可能与 RPM_Y 方向不同，禁止直接按相同数值解释。全 site 清单用于识别，不能直接扩充到原仅处理 fabric 原语的容量表。

## 5. 自动分类与首轮具体惩罚

下表是**待审阅的启发式初值**，不是已经从 Vivado 精确校准的物理延迟：

| 类型 | 识别/启用条件 | 首轮额外代价 |
|---|---|---|
| SLR 接缝 | SLR 与 clock-region/site 归属一致、拓扑已支持 | **1.5 ns/道接缝**，沿用现有参数 |
| 重要 I/O 带 | 已知类型＋tile 范围＋两侧 fabric，合并为同一物理带 | **0.5 ns/次完整穿越** |
| CONFIG/PCIe/CMAC 等局部硬 IP | 确认几何范围，候选连接确实需要穿越该区域 | **0.5 ns/次有效穿越**；无必经关系时不自动加罚 |
| 普通 DSP/BRAM/URAM 资源列 | 正常异构资源分布 | 初值 **0 ns 额外固定罚项**，保留原距离估计与资源可行性 |
| 普通 clock-region 分界 | 不与已识别的重要边界重合 | **0 ns** |
| 未识别类型、只有包围盒的低置信候选 | 证据不足 | **0 ns 自动附加**，报告未覆盖项，不标记为已验证边界 |
| PROHIBIT、固定占用、专用级联规则 | 真实用户约束或物理合法性 | 按既有硬约束处理，不用“很大延迟”代替非法判定 |

I/O 的 M/S/SNGL、相关控制资源以及同一硬 IP 的附属 tile 必须按物理区域归并、去重，不能逐 site 收费。端点本来属于硬 IP/I/O 时，不等同于横穿整个障碍；同一结构不可在几何线长、旧 X 修正和新固定代价中重复收费。

新模式的基本关系：

```text
D_new = D_original_distance_regression + 1.5 × SLR_crossings + other_confirmed_boundary_penalties
```

`D_original_distance_regression` 保留原三个距离区间与下限；新模式移除原按 X 列 2 等条件增加的经验项，改用真实结构。SLR 项也只添加一次。旧模式完整保留当前 b572999a 的行为，便于做 A/B。

局部障碍采用有限范围候选与区域邻接关系；比较有限个绕行/穿越候选，不把端点连线与包围盒相交直接视作必经。若几何只能证明“可能影响”，先不收固定穿越罚项，并保留候选报告。此处是几何代价近似，不模拟线资源、SLL 容量或路由拥塞。

参数评估使用已有 routed DCP 中的受约束数据连接，排除时钟、常量、未布线连接，按距离/扇出/资源类型和跨界组合分组比较残差。分组缺样或多种边界同时出现而无法区分贡献时保留初值并标注不确定性。实际布线绕行不能全部归因于某种边界；评估结果只形成建议，正式系数必须记录版本后再启用。

## 6. 二维聚拢算法

1. 用新延迟模型进行 STA，从负 slack 或接近目标的路径选择候选；继续利用现有图遍历，但不再仅按逻辑级数判定关键性。重叠簇采用确定性归属，避免同一 PU 被多个目标拉扯。
2. 确定可移动逻辑与固定端点。本轮聚拢器直接处理可移动 CLB PU；DSP/BRAM/URAM 及固定 I/O 作为候选评价的端点，其正常宏合法化流程仍可移动可移动硬资源。含 DSP 的路径不再整段放弃；端点移动后重新评价偏好。
3. 枚举当前区域、同 SLR 其他侧区及相邻 SLR 的少量候选区域。对多个宏、固定端点分布不适合整体移动的路径允许拆成段；不要求把整个设计或所有关键逻辑聚到一个 SLR。
4. 检查类型化容量及约束。复用现有兼容表和占用模型，分别核对 LUT/FF、SLICEM、DSP、BRAM 等效槽、URAM；不能把 LUT5/LUT6 或 RAMB18/RAMB36 等重叠资源重复计数，也不能把 SLICEM 当成与所有 SLICE 完全独立的容量。扣除固定/禁用占用，计入移入、移出与其他已接受簇的预留。容量只是必要条件，最终仍经过原打包与合法化检查。
5. 评价簇内与对外连接。使用同一延迟接口和时序关键性权重；比较最差路径改善、受影响连接的总延迟以及位移，拒绝只改善内部线却严重恶化外部线的目标。只接受预计有收益的变更，限制候选数量和单轮位移。
6. 创建区域软目标。对区域外的 PU，吸引至最近的可行内部位置；区域内的 PU 不强行拉向同一点。X/Y 分别启用吸引，强度沿用现有锚点权重尺度并受限。新模式不再运行原整列 Y 拉伸，使用区域内现有密度摊开并允许必要的区域外回退。
7. QP/摊开后刷新占用和时序；偏好失效、预算不足或收益消失时释放预留并重选/放弃。打包更改 PU 后清理旧指针/ID 再建目标。候选站点不足时必须终止有界搜索并回退，不能在空区域内无限寻找。

初始候选上限、软吸引权重和接受阈值沿现有尺度设置并写入运行报告；不通过改变 ClockPeriod 或关掉合法性检查取得表面收益。

## 7. 可审阅输出与验收

每轮导出：器件结构图、边界列表（原始坐标与 AMF 坐标）、类型/来源/置信等级/生效代价、未分类对象、区域容量与占用、每次聚拢的源/目标/预算/拒绝或回退原因，以及各阶段跨界统计。

统计至少包括：唯一 driver-sink 连接的穿越数、受约束关键路径的累计穿越数、SLR 往返次数、不同类型边界分别计数。区分 AMF 阶段、合法化/打包阶段与 Vivado 布线后结果；几何候选穿越与真实路由报告分开标注，不能把跨界路径数量称作 SLL 使用量。

验证分三层：

- **数据与原生测试**：U250 三条 SLR 接缝与 HPIO X4 的实测定位；换列号/方向/局部跨度的合成器件；未知类型、空 bank 关联、局部 IP 不贯穿全片、重叠边界去重、端点处于硬 IP、坐标方向/中点、旧输入与错误哈希。
- **聚拢测试**：SLR 往返路径、I/O 两侧路径、DSP/URAM 端点、多个簇竞争容量、SLICEM 紧张、固定单元、宏不可拆、区域内无中心坍缩、预算释放、打包后目标重建、容量不足回退、关闭新模式的 VCU108 回归。
- **完整 GETRF 对照**：相同 DCP、时钟、二进制与主要配置下，比较现有模式、仅物理边界延迟、边界延迟＋二维聚拢。若需归因于 SLR 系数，再加 SLR=0 消融；每轮独立记录。检查覆盖率、级联、DRC、布线完整性、WNS/TNS、跨界统计、位置保留率与运行开销。

代码通过不等于 QoR 通过。若聚拢模型测试通过但完整 GETRF 时序/可布线性变差，保留失败实验，新配置不晋升为默认验证基线；按阶段统计定位原因后修正。

## 8. 建议实施顺序

| 阶段 | 交付 | 验收后再进入下一步 |
|---|---|---|
| A | 完整结构导出、坐标契约、分类规则、边界与区域报告 | 人工核对 U250 结构图和未知对象；原 fabric 数据保持一致 |
| B | C++ 模型与统一边界延迟、旧模式兼容、测量样本 | 原生回归和代价去重通过；参数来源可追溯 |
| C | 区域预算、二维聚拢、QP/摊开/候选搜索协同 | 小网表验证吸引方向、资源竞争、回退和硬约束 |
| D | GETRF A/B、阶段性审计与复现记录 | 用布线后指标决定是否采用新默认策略 |

本方案待用户审阅后才开始 A 阶段。仅报告中已实测的器件对象属于现有能力；上表文件、数据结构、参数分类与二维聚拢均为拟议改动。
