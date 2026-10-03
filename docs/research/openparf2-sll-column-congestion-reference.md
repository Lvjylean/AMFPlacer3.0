# OpenPARF 2.0 的 SLL 统计与 AMFplacer 逐列拥塞模型参考

本文服务于 AMFplacer3.0 的 U250 多 SLR 布局研究。结论是：**OpenPARF 2.0 可以参考其跨 SLR 网络统计和可微目标函数，但不能直接提供每个物理 SLL 列的使用量或拥塞率。AMF 需要增加独立的 SLL 资源模型、逐列需求估计，并用 Vivado 布线后的节点占用校验。**

审阅对象是官方 `2.0.0` 标签，提交 `eb1b6bce9be9da992b285a85d572a7f9c76fe92a`，服务器目录为 `/Projects/jinyang/workspace/AMFplacer3.0/data/reference/openparf-2.0`。本文没有修改该源码，也没有向 AMF 的生产布局目标函数加入新的代价。以下“建议”“拟新增”均表示设计参考，不表示功能已经实现。

## 源码能回答什么

| 模块与固定版本源码 | 实际行为 | 不提供的信息 |
| --- | --- | --- |
| [SLL CPU 内核](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/sll/src/sll_kernel.cpp#L19) | 根据每个 net 的引脚分布生成 SLR 位图，然后查表 | 具体 SLL 节点、物理列、容量、实际布线路径 |
| [SLL 包装层](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/sll/sll.py#L66) | C++ 返回逐 net 的结果，Python 在第 81 行求总和 | 逐边界和逐列分解 |
| [SoftFloor](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/soft_floor/src/soft_floor_kernel.cpp#L13) | 用 sigmoid 的和近似离散 SLR 编号 | 穿越哪一列 |
| [WASLL](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/wasll/src/wasll_kernel.cpp#L32) | 对平滑 SLR 坐标计算加权平均范围，提供优化梯度 | 列容量或列超额惩罚 |
| [RUDY](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/rudy/rudy.py#L68) | 根据 net 包围盒估计二维水平、垂直布线需求并归一化 | SLL 专用跨界通道占用 |
| [拥塞预测模块](https://github.com/PKU-IDEA/OpenPARF/blob/eb1b6bce9be9da992b285a85d572a7f9c76fe92a/openparf/ops/congestion_prediction/congestion_prediction.py#L54) | 用水平、垂直需求及引脚密度进行模型预测；含 `168×480` 形状假设 | 可直接用于 U250 的逐列 SLL 资源账本 |

源码中的 `_sllSiteColumnArray` 也不能按名称当作 Super Long Line 列表：`openparf/ops/clock_network_planner/src/database_wrapper.h:98` 明确注释为 **SLICEL site columns**。

## SLL 计数的准确含义

CPU 内核处理的是交错存储的引脚坐标 `x0,y0,x1,y1,...`，不是某些注释所写的先全部 x 再全部 y。它根据统一 SLR 宽高计算引脚的 SLR 编号，标记 net 覆盖哪些 SLR，然后查询 `sll_counts_table`。

对于 `1×4`、SLR 按物理纵向顺序编号的模型，一个非空 net 的查表结果等价于：

\[
T_n=\max_{p\in n}s(p)-\min_{p\in n}s(p).
\]

这里的编号是按位置排序的内部索引，不能未经映射直接使用器件的 SLR 名称后缀。引脚在 SLR0 和 SLR3 时，结果为 3；同一侧增加 100 个 sink 不会直接变成 300。它表示这个端点分布在串联拓扑下至少需要跨越的接缝数，**没有执行真实路由，不能当成最终占用的 SLL 根数**。路由复制、分支和绕行可能使实际占用不同。

`sll.cpp:55–69` 返回形状为 `[num_nets,1]` 的 `partial_sll`。需要逐 net 数据时，可以参考这个接口；仅去掉 Python 的求和不会自动得到逐列数据。`net_weights` 虽在接口中传入和检查，CPU 计数内核没有用它乘以结果，因此该计数不是时序权重加权的 SLL 指标。

在 `1×4` 中，`WASLL` 内核只在 `num_slrX>1` 时计算 x 方向项。保持引脚 y 和 SLR 分布不变、将所有跨界网络横向集中或分散，其 SLL 计数以及 WASLL 这一项都不因此反映列负载差异。其他线长项可能改变布局，但不能替代 SLL 列容量约束。

### 复制代码前需要修正或明确的口径

固定版本中存在明确的 CPU/GPU 过滤差异：

| 实现 | 纳入计数的条件 |
| --- | --- |
| `sll/src/sll_kernel.cpp:25` | `net_mask[i] || pin_num < 2000` |
| `sll/src/sll_cuda_kernel.cu:27–29` | `pin_num <= 2000 && net_mask[i]` |
| WASLL 调用链 | 使用 `net_mask_ignore_large`，该掩码在 `data_collections.py:392–393` 定义为 pin 数 `<1000` |

例如，mask 为 1、pin 数为 2001 的网络会被 CPU 计数纳入，但被 GPU 计数排除；mask 为 0、pin 数为 2 的网络也有 CPU/GPU 差异。默认 `net_mask` 初始化为全 1，见 `data_collections.py:384`。因此不能默认该标签的 CPU 与 GPU 总 SLL 数具有相同统计范围。这是源码审阅结论，尚未在已编译的 OpenPARF 上运行对照。

此外，CPU 和 GPU 内核都使用 `slr_dist[4]`，SLR 坐标索引处没有边界钳制。扩展超过四个 SLR，或处理器件范围外的位置时，不能只补查找表。AMF 应检查坐标、实际 SLR 映射和未知位置，避免越界或静默错计。

## OpenPARF 怎样间接缓解局部 SLL 热点

继续沿调用链检查后，可以明确：本版本既有全局 SLL 最小化，也有局部移动的 SLL 数量代价；但没有根据物理 SLL 列使用率进行反馈的机制。这里的“局部移动”指移动或交换少量实例，不等于“逐列热点感知”。

1. **全局布局减少跨界需求。** `placement/place_model.py:30–72` 的基础目标为 `WAWL + psi * WASLL + density`，启用对应阶段时加入 SLL 项。它鼓励连接紧密的实例减少跨 SLR 分布。`placer.py:2545–2571` 根据前后两次的总 SLL 数变化，经 EMA 和 Adam 风格的一、二阶矩更新标量 `psi`；输入不是逐列占用图。
2. **时钟区域分配考虑跨界增量。** `ops/clock_network_planner/src/utplacefx/ClockNetworkPlanner.cpp:1151–1158` 在启用 SLR awareness 且实例组符合 SLL 优化条件时，将 `computeSLLIncrease` 加入分配代价。`2606–2607` 的增量来自 SLR 级包围盒宽、高的变化。这里存在逻辑资源与时钟约束，但不是物理 SLL 列容量约束。
3. **详细布局避免增加跨界范围。** `ops/ism_dp/src/ism_solver.cpp:869–888` 将 `sllIncreaseCost` 加入实例到候选位置的匹配代价；它使用候选 pin 的 SLR 索引相对 net 其余引脚 SLR 包围盒的距离。它可以改善少量实例移动的跨界数量，但无法区分两个候选位置分别靠近满载 SLL 列还是空闲 SLL 列。
4. **普通拥塞通过面积膨胀缓解。** `placement/placer.py:293–321` 在启用 `gp_adjust_route_area` 时读取 RUDY 或预测拥塞图，再交给面积调整。`placement/functor/adjust_inst_area.py:149–189,220–223` 计算需求面积并放大实例的虚拟尺寸，使后续密度优化倾向于疏散拥挤区域。若普通布线热点与 SLL 热点重合，可能间接改善；源码没有给出这种相关性的保证。

例如，方案 A 与 B 都有 2000 个拓扑跨界需求，A 分散到多列，B 集中到一列。只要引脚的 SLR 分布一致，SLL 计数以及 `1×4` 中的 WASLL 项不会因这个横向分布差异而区分 A/B。线长、普通密度或 RUDY 可能区分它们，但没有表达该列还剩多少 SLL 的容量条件。最终具体 SLL 节点仍由后端路由决定，不能将后端选路的效果归因于 OpenPARF 已有逐列拥塞模型。

对 AMF 的设计含义是：可以复用“全局跨界需求 + 局部移动增量”的两层思路，同时新增下面的逐列需求/容量项。仅提高统一的 SLL 权重可能减少总跨界量，但不能保证疏散一列中的热点。

## 每列的定义与指标

建议以 **物理 SLR 接缝 × SLL 物理列** 建立资源桶。U250 有三道相邻 SLR 接缝，同一个 X 列在不同接缝上是不同资源桶。以下概念必须区分：

- `num_slrX` 是芯粒拓扑的列数，不是 SLL 列数。
- CLB 的 `SLICE_X`、时钟区域的 X 和 Laguna 的列坐标不属于同一坐标空间。
- 时钟区域列可用于粗粒度汇总，但不应冒充更细的物理 SLL 列。
- 列键应保留器件数据库中的端点 tile/site 信息以及统一坐标映射，不通过字符串中的 X 数值猜测 AMF 几何坐标。

对于接缝 b、列 c，定义：

| 指标 | 定义 | 用途 |
| --- | --- | --- |
| `physical_capacity` | 该桶内去重的物理 SLL 资源数 C | 器件固有资源清单 |
| `effective_capacity` | 扣除已明确禁止或预留资源后的容量 C_eff | 指定设计和约束下的模型容量；未知时不可假造 |
| `routed_used` | 最终路由占用的去重 SLL 资源数 U | 布线后真实使用量 |
| `routed_utilization` | U/C，或显式标注的 U/C_eff | 布线后占用比例 |
| `estimated_demand` | 布局阶段模型分配到该桶的需求 D | 放置阶段反馈 |
| `estimated_load` | D/C_eff | 可超过 1 的拥塞代理 |
| `estimated_overflow` | max(0,D−C_eff) | 估计超额资源数 |
| `headroom` | C_eff−U | 给定容量口径下的已布线余量 |

不要将这几个值统一命名为不带说明的“拥塞率”。合法的最终路由一般不会让同一独占 SLL 资源被重复占用，因此 U/C 不会表现为大于 100% 的过载；它不能反推出路由前被迫绕行的全部需求。D/C_eff 则允许超过 100%，但只是模型预测。

例如，某列可用 1440、实际使用 1200，则使用率为 83.33%；若布局模型预测需求 1600，则预测负载 111.11%、超额 160。这是**说明定义的假设例子**，不是某个 U250 列的实测结果。零容量列且 D>0 时应标记不可满足；不要用很小的分母把它伪装成普通百分比。

若上下行共享同一组可选物理资源，总容量不能给两个方向各算一次。方向独立容量必须从器件资源方向性确认。方向需求、占用可以作为附加字段，初始实现先保证无方向合计准确。

## 布局前怎样估计每列的需求

以下是建议给 AMF 增加的模型，不是 OpenPARF 已实现的功能。

### 先得到每个 net 的接缝需求

根据驱动和负载的真实 SLR 分布，计算 net 在哪些接缝上必须连接两侧。对于串联的 `1×4`，遍历最小、最大 SLR 位置之间的每道接缝。每个 `(net,boundary)` 先产生一个拓扑需求单位。

这一步按 net 和接缝去重，不按全部 driver-sink 对累计；100 个对侧 sink 可以共享一次跨界。每个总线 bit 通常是独立的信号 net，需要各自计数。时钟、常量、硬 IP 专用网络应按资源类型分类，不能因为高扇出就静默抛弃，也不能将它们全部视为普通数据 SLL。未知外部端口位置另列，不能默认落在 SLR0。

### 再分配到候选列

只有引脚位置不能唯一确定实际 SLL 列，需要显式的路由代理。第一版建议对每个 `(net,boundary)` 构造候选列及概率，而不是假称已经知道真实路线：

\[
p_{nbc}=\frac{a_{nbc}\exp(-\Delta L_{nbc}/\tau)}{\sum_{c'}a_{nbc'}\exp(-\Delta L_{nbc'}/\tau)},\qquad
D_{bc}=\sum_n m_{nb}p_{nbc}.
\]

`a` 是候选可达性掩码；`ΔL` 是到该列的额外线长或经验证的路由代价；`τ` 控制分布宽度。最初令 `m_nb=1`，表示拓扑最小需求。要加入多分支重复跨界的放大量，应从实际 routed 数据标定，不能直接令它等于 fanout。

初版可以用跨界两侧引脚 x 区间到候选列的距离构造代价，后续改为粗路由树穿越位置。包围盒给出候选范围，不意味着范围外的列绝对不可达。对跨多道接缝的 net，独立分配每道接缝只是一阶近似；进一步可用动态规划限制相邻接缝列选择的大幅横向跳变，并加入中间 SLR 的绕线成本。

仅凭 x 接近还不能宣称可达，硬 IP、布线障碍、专用资源、DFX 限制等应进入资源模型或留作未建模限制。不存在有效候选列时，输出未分配需求，不能通过归一化将其吞掉。

### 将需求转为 AMF 可用的代价

建议先作为独立诊断输出，校准后再加入布局代价，例如：

\[
E_{SLL}=\lambda\sum_{b,c}C^{eff}_{bc}\left[\max\left(0,\frac{D_{bc}}{C^{eff}_{bc}}-\rho_0\right)\right]^2.
\]

`ρ0` 是项目选择的预留裕量阈值，不是硬件规定。资源容量单位必须是 SLL 数；时序权重可以进入目标函数，不能把加权目标值称为物理占用。AMF 不必整体改造成 PyTorch/GPU 框架：C++ 中按 net 并行统计或维护移动影响网络的增量需求即可。

## RUDY 可以借鉴的部分与边界

`rudy_kernel.hpp:74–98` 将 net 包围盒与 bin 的重叠面积乘以网络权重及 fanout 修正，然后分别除以包围盒高、宽，累加到水平和垂直需求图。`rudy.py:94–103` 再除以 bin 面积与方向容量，最后取两个方向的最大值。

这种“累加需求，再除以容量”的结构可以借鉴，但 **RUDY 的二维普通布线容量不能用作接缝 SLL 容量**。`params.json` 的默认水平、垂直容量分别为 209 和 239；`op_collections.py:248–249` 将它们作为全局参数传入，而不是读取 U250 每列 SLL 清单。复制这两个数字不会得到真实 SLL 利用率。

还要处理退化包围盒：该版本即使分母加了 epsilon，重叠面积在零宽或零高时仍可能为零。不能照搬二维面积重叠公式后，让某些恰好垂直的跨界 net 在 SLL 需求图中消失。SLL 接缝需求应通过上述 net-cut 逻辑产生。

## 布线后怎样取得真实的逐列占用

服务器 Vivado 2024.2 的实机探测已经确认以下接口可用于 `xcu250-figd2104-2L-e`：

```tcl
# 先只读打开已有 routed DCP。
open_checkpoint /path/to/routed.dcp
set pair [get_slrs {SLR0 SLR1}]
set all_sll [get_nodes -slls_between $pair]
set used_sll [get_nodes -slls_between $pair -filter {IS_USED == 1}]

# 每个 node 是计数单位；两端 tile 不能再分别计数。
foreach node $all_sll {
    set tiles [get_tiles -of_objects $node]
    set columns [lsort -unique [get_property COLUMN $tiles]]
    # 本次 U250 中要求两个端点的 COLUMN 一致；不一致应中止并审查。
}
# 对 used_sll 中的 node 可用 get_nets -of_objects $node 追溯网络。
```

对每道接缝：将 `all_sll` 中节点按端点 tile 的 `COLUMN` 分组，组大小为物理容量；将 `used_sll` 按同样规则分组，得到已用数量。以 node 的规范 `NAME` 去重。保留全量节点清单，用它审计列汇总，而不是只输出一个百分比。

实机样本 `LAG_LAG_X4Y539/UBUMP0` 的属性包括 `IS_SLL=1`、`IS_CROSSING_SLRS=1`、`CROSSING_SLRS={SLR1 SLR2}`、`INTENT_CODE_NAME=NODE_LAGUNA_DATA`；其两个 tile 的 `COLUMN` 都是 **71**。这里名称中的 `X4` 不等于数据库 `COLUMN=71`，也不能直接当成 AMF 的布局 x 坐标。

还有一个接口细节：探测中未使用节点的 `list_property` 列表没有 `IS_USED`，而使用节点有 `IS_USED=1`。因此不要检查第一个节点是否具有该属性，就据此跳过整道接缝的占用统计；应使用上述集合筛选。空的使用集合要正常生成全零列，不能对空集合直接调用要求非空对象的属性查询。

已准备两个原始诊断工具和一个审计工具：

- `scripts/diagnostics/probe_sll_columns.tcl`：检查对象属性、端点及版本。
- `scripts/diagnostics/export_sll_columns.tcl`：导出全量节点清单、逐列与逐接缝统计，生成原生利用率报告。当前显式限定本次 U250 part，不宣称支持所有 FPGA。
- `scripts/diagnostics/analyze_sll_columns.py`：检查唯一性、节点到列汇总、使用率计算、原生每接缝及总量一致性；不一致直接报错。

这些工具只读取既有 DCP、写诊断报告，不执行 `place_design`、`route_design` 或 `write_checkpoint`。结果 DCP 不下载到本地。

在服务器重用时，为每次分析选择新的 `experiments/preflight/<唯一目录>`，并按项目规范记录 DCP 哈希、脚本哈希和退出码。基本调用形式如下，尖括号内容须替换为本轮实际路径：

```sh
/Projects/Xilinx/Vivado/2024.2/bin/vivado \
  -mode batch -nojournal -log <新输出目录>/vivado.log \
  -source scripts/diagnostics/export_sll_columns.tcl \
  -tclargs <已有routed.dcp绝对路径> <新输出目录>

python3 scripts/diagnostics/analyze_sll_columns.py <新输出目录>
```

审计脚本要求输出目录中的 `manifest.json` 已记录成功的 `exit_code=0` 以及 `input_dcp`、`input_sha256`；它不会将尚未完成的输出当成成功结果。本次运行的 manifest 与冻结脚本保存在后文证据目录中，供后续建立统一诊断入口参考。

需要追溯“满载列中是哪一些网络”时，可读取 `sll_nodes.tsv`，筛选 `boundary`、`column` 和 `used=1`，再查看 `node` 与 `nets`。同一 net 出现多行可能代表真实使用多个 SLL 节点；汇总占用时不能按 net 去重。

## AMF 代码落点与数据接口

目前 AMF 已有物理边界和引脚位置基础，但没有在这些结构中提供逐列 SLL 容量。

| 现有位置 | 可复用内容 | 拟增加能力 |
| --- | --- | --- |
| `src/lib/HiFPlacer/deviceInfo/DeviceInfo.h:342` | site 的 SLR ID | 容量模型的器件关联 |
| `src/lib/HiFPlacer/deviceInfo/PhysicalBoundaryModel.h:24` | 接缝位置、范围及惩罚 | 独立 `SllColumnResource`，不能把逻辑资源容量误用为 SLL 容量 |
| `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h:1894` | net 引脚、cell/macro 内偏移、PlacementUnit 坐标 | 从真实 pin 位置生成 net-cut 需求 |
| `src/lib/HiFPlacer/designInfo/DesignInfo.h:1628` | 原始 net/pin 集合 | 稳定 net ID、资源类别与排除原因 |
| `scripts/build_physical_boundaries.py` | 器件坐标与接缝模型 | 关联资源清单及原始坐标，记录器件/版本/哈希 |
| `scripts/diagnostics/finish_r10_full_flow.py` | 解析原生 SLL 总量和每接缝数量 | 核对逐列求和与 Vivado 原生报告是否一致 |

建议独立保存以下文件，避免每次布局重新扫描 Vivado 器件数据库：

```text
sll_resources.tsv: part, boundary, column_id, node_id, endpoint_a, endpoint_b,
                   tile_a, tile_b, physical_x, resource_direction
sll_columns.tsv:  boundary, column_id, physical_capacity, effective_capacity,
                   estimated_demand, estimated_load, estimated_overflow,
                   routed_used, routed_utilization
sll_net_usage.tsv: net_id, boundary, column_id, node_id
```

字段未知时保留 unknown/null，不能填写 0。资源清单适合按 part、Vivado 版本及数据库哈希缓存；设计相关禁止/预留、需求和占用必须按本轮设计重新计算。

C++ 侧建议划分为器件资源表、net-cut 需求构造、列分配、审计输出四个模块。接入 SA 或其他移动优化时，只撤销并重算受影响 net 的桶贡献；进行候选移动评估时不能直接污染正式计数，接受移动后才提交差量，回滚必须守恒。接入 QP 需要额外设计平滑可导近似，不能把一个离散直方图直接称为可用梯度。

下面是表达接口关系的 C++ 伪代码，函数名为拟新增接口，不是已经存在于 AMF 的可编译调用：

```cpp
for (const auto& net : originalDesignNets) {
    // 原始信号 net 是基本单位，不在 driver-sink 对上重复累加。
    auto pins = resolvePinPositionsAndSlrs(net, placement, device);
    if (!pins.allLocationsKnown()) {
        audit.recordUnknown(net.id());
        continue;
    }
    auto resourceClass = classifyRoutingResource(net);
    if (resourceClass != OrdinaryDataSll) {
        audit.recordSeparateClass(net.id(), resourceClass);
        continue;
    }
    for (auto seam : requiredSeams(pins)) {
        auto candidates = reachableColumns(net, seam, sllResources);
        if (candidates.empty()) {
            audit.recordUnassigned(net.id(), seam, 1.0);
            continue;
        }
        auto probabilities = normalizeCandidateCosts(pins, seam, candidates);
        for (const auto& candidate : probabilities) {
            demand[seam][candidate.column] += candidate.probability;
        }
        // 同时缓存本 net 的贡献，便于之后撤销、重算和审计。
        contributions.store(net.id(), seam, probabilities);
    }
}
```

每个正常分配的 `(net,seam)`，所有候选列概率之和应为 1。未分配需求、未知位置和另列资源类别分别统计，三者都不能计为“需求为零”。若以后引入 `m_nb>1`，应同步记录该模型放大量及其标定来源。

## 验证要求

1. 逐列物理容量求和与原生每接缝容量一致，逐列实际占用求和与 `report_utilization` 每接缝 Used 和 Total SLLs Used 一致。若不一致，首先检查方向、节点别名、端点重复计数、常量/时钟和报告阶段。
2. 同一 net、同一 SLL 节点只计一次；一个 net 使用两个不同 SLL 节点必须计两次。不能用唯一跨 SLR net 数替代节点数。
3. 一个 net 跨三道接缝、许多 sink 共享一次跨界、同一 SLR 内两 pin、位置未知、零容量列、多分支重复跨界均需覆盖。移动前后增量计数必须等于全量重算。
4. 将预测结果与多种布局的最终占用比较：逐列误差、热点列排序、最忙列误差、未分配需求。总 SLL 误差很小仍可能漏报局部热点。
5. 相同输入、器件、约束、工具版本、后端策略下，再比较实际 SLL、拥塞、WNS/TNS 与时间。验证集应包含新的布局或算例，不能仅拟合用于标定的一次路由。

推荐实施顺序是：**真实资源与占用采集 → net-cut 代理统计 → 逐列需求分配及校准 → 布局目标函数反馈**。这样每一步都有独立可检查的数据，不会将论文中的总 SLL 代理目标误认为已经实现了 U250 逐列拥塞优化。

## 官方接口依据

- [AMD UG835 get_nodes](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/get_nodes)：列出了 `-slls_between` 查询接口；网页版本可能比服务器更新，实际使用以服务器 Vivado 2024.2 探测为准。
- [AMD UG906 Report Utilization](https://docs.amd.com/r/en-US/ug906-vivado-design-analysis/Report-Utilization)：原生资源报告包含 SLR Crossing Utilization，可作为汇总核对。
- [AMD UG574 Flip-flops](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Flip-flops)：描述 Laguna site/tile 与 SLL 关联。寄存器数量不等于 SLL 使用量，未放 TX/RX 寄存器也可能占用 SLL。

## U250 实测样例

本次只读分析使用此前 AMF3 完成的 MiniMap2 完整设计布线结果：

```text
run: amf3-minimap2-u250-full-r10-import-repair-full-20260930-211203-742548
DCP: reports/getrf_routed.dcp
SHA256: 5f4c150e9ca80be320c60e017d46d060d46f066c63f28745db5f3e9a428c0cb2
part: xcu250-figd2104-2L-e
Vivado: 2024.2
```

`getrf_routed.dcp` 是现有通用后端的历史输出文件名，本轮输入设计是 MiniMap2，不是 GETRF。该样本用于证明采集方法及展示列分布，未启动新的布局布线，不涉及修改 8 ns 时序约束。

已采集的 SLR0↔SLR1 接缝有 16 个物理列，每列 1440 个 SLL 节点，合计 23040。实际使用 7820、接缝整体占用率 **33.94%**，但 `COLUMN=673` 与 `COLUMN=713` 都使用 **1440/1440，即 100%**。因此“总量有余量”不能推导出“所有列都有余量”。

100% 是该布局最终选择并占用满列的事实，并不单独证明路由失败，也不证明某条关键路径的 WNS 由此造成。判断拥塞导致的绕线和时序影响，还要结合路由前需求、相邻接入布线、路径延迟及对照布局。

本次完整采集成功，69,120 个唯一物理 SLL 节点、48 个接缝与列组合全部核对通过；实际使用 16,580 个，与新生成的 Vivado 原生报告逐接缝及总量完全一致。三个接缝的每列容量均为 1440，本次没有 `IS_BAD=1` 的节点。

| 物理 COLUMN | SLR0↔SLR1 已用与占比 | SLR1↔SLR2 已用与占比 | SLR2↔SLR3 已用与占比 |
| --- | --- | --- | --- |
| 71 | 0 / 0.00% | 239 / 16.60% | 0 / 0.00% |
| 110 | 1 / 0.07% | 845 / 58.68% | 0 / 0.00% |
| 156 | 61 / 4.24% | 729 / 50.62% | 0 / 0.00% |
| 189 | 108 / 7.50% | 559 / 38.82% | 0 / 0.00% |
| 228 | 183 / 12.71% | 827 / 57.43% | 0 / 0.00% |
| 275 | 356 / 24.72% | 574 / 39.86% | 0 / 0.00% |
| 310 | 478 / 33.19% | 493 / 34.24% | 0 / 0.00% |
| 349 | 297 / 20.62% | 859 / 59.65% | 0 / 0.00% |
| 438 | 638 / 44.31% | 832 / 57.78% | 0 / 0.00% |
| 476 | 509 / 35.35% | 799 / 55.49% | 0 / 0.00% |
| 515 | 521 / 36.18% | 558 / 38.75% | 0 / 0.00% |
| 549 | 455 / 31.60% | 610 / 42.36% | 0 / 0.00% |
| 595 | 553 / 38.40% | 306 / 21.25% | 0 / 0.00% |
| 629 | 780 / 54.17% | 290 / 20.14% | 0 / 0.00% |
| 673 | 1440 / 100.00% | 240 / 16.67% | 0 / 0.00% |
| 713 | 1440 / 100.00% | 0 / 0.00% | 0 / 0.00% |
| 合计 | 7820 / 33.94% | 8760 / 38.02% | 0 / 0.00% |

服务器证据目录：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261001-sll-column-export-163408`。其中 `sll_nodes.tsv` 保留完整逐节点与网络关联，DCP 留在原实验目录。轻量副本位于本地 `local-reports/20261001-sll-column-export-163408/`。

[逐列 TSV](../../local-reports/20261001-sll-column-export-163408/sll_columns.tsv) · [一致性审计](../../local-reports/20261001-sll-column-export-163408/audit.json) · [原生利用率报告](../../local-reports/20261001-sll-column-export-163408/utilization.rpt)

原始对象探测成功记录为 `20261001-sll-column-probe-162259`。首次完整导出在零占用集合处理处失败，保留于 `20261001-sll-column-export-162851`；最终成功记录使用修正后的脚本重新读取同一个 DCP，未补写虚构的零值，也未改写原布线实验结果。
