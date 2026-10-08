# U250 半列时钟容量：数据库读取与边界验证

2026-10-04，eda072，目标 `xcu250-figd2104-2L-e`，Vivado 2024.2（SW Build 5239630）。

**对本次检查的 U250 SLICE 专用叶时钟资源，名义容量为 16；原 AMF/ISPD 的 12 不能称为 U250 的物理硬上限。** 本轮既从器件节点连接图读取了资源，又进行了同一半列内 13、16、17 个独立时钟的验证。读取结果已经可选接入 AMF 容量表。

## 三类证据

| 证据 | 实际结果 | 能说明什么 |
| --- | --- | --- |
| U250 空器件数据库连接图 | 每个检查的 SLICE 半列端点 CLK 输入可达 16 个不同的 `NODE_GLOBAL_LEAF`；每个叶输入可达 24 个 HDISTR 节点 | 专用叶资源的名义数量及真实共享关系 |
| `get_param place.maxNumClocksInHalfColumn` | 本环境打开器件前、`link_design` 后均为 16；查询未修改参数 | 当前 Vivado 软件布局限额；与物理资源数分别记录 |
| 同半列微型设计 | 13/16 个时钟均全部布通，17 个出现明确叶资源过量与冲突 | 12 并非该半列硬上限；16 个资源确能同时使用，17 个触发相应瓶颈 |

17 时钟实验中，Vivado 报 `Route 35-326`（leaf clock resources over utilization），并指出 `gclk_9`、`gclk_7` 争用同一节点 `RCLK_INT_L_X74Y389/CLK_LEAF_SITES_14_CLK_LEAF`。最终 16 条时钟网络为 `ROUTED`，1 条为 `ANTENNAS`，`route_design` 失败。

该错误同时建议 `set_param place.maxNumClocksInHalfColumn 12`。因此，12 可以作为降低时钟拥塞风险的保守布局预算，但它与默认值 16、物理叶容量 16 是不同概念。不能把 16−12=4 自动解释成器件固定预留了 4 条轨道；本轮没有这样的证据。

## 数据如何自动取得

`scripts/export_clock_half_column_topology.tcl` 使用空器件 `link_design`，不读取 benchmark 布局、不执行 placement/routing。主要查询链为：

```text
SLICE CLK1 / CLK2（SLICEM 另含 LCLK）
  → get_nodes -of_objects <site_pin>
  → 两层 get_nodes -uphill -of_objects <已有节点>
  → 按 INTENT_CODE_NAME == NODE_GLOBAL_LEAF 筛选并去重
  → 关联 BUFCE_LEAF 输出和 CLK_IN
  → 查询 CLK_IN 的 HDISTR 上游节点
```

容量由节点集合的基数计算，代码没有把 12 或 16 写成查询结果。整个流程也没有用 BUFCE_LEAF site 总数直接代替某个半列的容量。相关查询使用已有对象，避免重复按节点名称枚举全器件。

全片检查包括 128 个 clock region、3,600 个区域内 SLICE 列、7,200 个上下半段，共检查 35,392 个时钟输入端点。所有半段读取到的可达叶数均为 16；同一半段的 CLK1、CLK2 及 SLICEM LCLK 叶集合一致。

这里保留一个明确的验证范围：先检查每个区域列有连续 60 行 SLICE，按上下 30 行划分；连接图检查每半段首末两个 site 的全部时钟输入，**没有逐个查询中间 28 行**。60/30 是经过实际几何核验的分组假设，不是推导时钟容量的除法。

按实际叶节点集合合并共享资源后，7,200 个 SLICE 半段对应 **4,736 个共享叶资源域**，共 75,776 个唯一叶节点；一个域包含一列或两列的半段。例如 X4Y6 中 X118、X119 的下半段共享同一组 16 个叶节点，不能各计 16 条独立容量。上半与下半使用不同叶集合。

`scripts/analyze_clock_half_columns.py` 核验逐 pin 证据、完整 SLICE 覆盖、part/Vivado 版本、AMF 设备 ZIP 身份，以及共享叶集合。它还从**实际 AMF ZIP 中的 tile X/Y**核验当前 AMF 的占用半列分组：要求同 CR 的各 SLICE 列具有同一连续 60 行范围，按 CR 最小 siteY 划分两半，并检查 tile X 的资源共享分组与数据库一致。部分重叠、上下分段错位或分组不匹配时，不允许将容量直接接入现有模型。

## 同时使用的微型验证

微型设计在 OOC 模块内部显式实例化 N 个 BUFGCE 和 N 个 FDRE，每个时钟来自独立顶层输入，每个 BUFGCE 只驱动自己的 FDRE。所有 FF 固定在 `SLICE_X117Y360` 起的同一个下半列，每行一个；保留单元和网络，核验没有合并。时钟周期为 10 ns，仅作为该资源探针的统一约束。

| 时钟数 | 结果 | 实际时钟网络状态 | 不同叶节点数 |
| ---: | --- | --- | ---: |
| 13 | 布局、布线成功 | 13 条 ROUTED | 13 |
| 16 | 布局、布线成功 | 16 条 ROUTED | 16 |
| 17 | 布线失败，明确叶资源过量 | 16 条 ROUTED、1 条 ANTENNAS | 16 |

没有使用 `CLOCK_DEDICATED_ROUTE FALSE`，也没有实例化或手工放置 BUFCE_LEAF。OOC 顶层输入/输出未模拟板级 I/O，日志相应提示端口外部路径与时序信息不完整；验证目标是 **BUFGCE 输出到固定 FF 的芯片内部专用时钟网络**，这部分网络已逐条核验。结果不是板级实现或时序收敛结论。

## AMF3 接入与使用

在服务器项目根目录运行：

```bash
python3 scripts/amf3.py prepare-clock-capacity \
  --part xcu250-figd2104-2L-e \
  --device data/devices/u250-vivado-2024.2/exportSiteLocation.zip \
  --query-half-columns
```

该选项自动查询全片半列连接图，并用每个区域实际读取的容量生成 `clock_capacity.tsv`。可用 `--half-column-topology-dir <raw-dir>` 复用完整原始查询，两选项互斥。轨道导出可另用 `--raw-dir` 复用，但始终核对 part、版本和设备文件。

未指定这两个选项时，为兼容旧实验保留 12，并将来源明确标记为 `legacy-amf-ispd-placement-budget`。指定数据库拓扑后，来源为 `vivado-device-node-connectivity-endpoint-sampled`，本次 U250 生成值为 16。**原有历史表不会自动改写。**

本轮用于 CLK-FPGA08 设备 ZIP 的新表位于服务器：

```text
experiments/preflight/clock-capacity-20261004-095524-713412/clock_capacity.tsv
```

表 SHA-256：`32751afd8f965be9d6afe3d3e5688bdaaf308005f37b1baa0bc61bd4cc24ac4c`。
该表绑定包含固定 I/O site 的实际设备文件；标准 U250 ZIP 应单独生成绑定表，不能只改文件路径。

使用前一阶段已验证、支持容量 schema 1 的构建 `build-20261004-085551-471666-5ea1f494`。本轮没有修改 C++，也没有改变默认构建。新的真实输入配置为：

```text
experiments/evidence/20261004-u250-half-column-capacity/clk08-u250-device-derived-half-capacity.json
```

真实输入读取已完成，结果位于 `experiments/preflight/input-inspection-20261004-095751-500060`：469,497 个单元、481,806 条网络、32 个时钟，128 个区域的半列容量均为 16，没有执行该 benchmark 的 placement/routing。

最终代码通过 18 项拓扑测试、15 项原容量测试和 30 项相关流程测试，共 63 项；`git diff --check` 通过。增加 Y 分组防护后，复用完整数据库证据在 `experiments/preflight/clock-capacity-20261004-101050-313300` 重新生成表，SHA-256 与上述已被 AMF 读取的表完全一致。源码提交、未提交文件哈希、工具版本、输入身份与各阶段耗时/退出码均保存在记录中。

## 可复查记录

服务器完整记录位于 `/Projects/jinyang/workspace/AMFplacer3.0`：

- 正式自动查询与容量表：`experiments/preflight/clock-capacity-20261004-095524-713412`。
- 独立全片数据库扫描：`experiments/preflight/20261004-u250-half-column-topology-full-094725-466167`。
- 软件参数读取：`experiments/preflight/20261004-u250-clock-policy-095005-477844`。
- 13 / 16 / 17 时钟微实验：`20261004-u250-half-column-13clocks-094311-267794`、`20261004-u250-half-column-16clocks-094640-573825`、`20261004-u250-half-column-17clocks-094644-885241`，均在 `experiments/preflight/` 下。
- 早期图查询因逐节点名称查找导致重复全器件枚举而被停止，保留在 `20261004-u250-clock-leaf-graph-093556-670219`；修正为对象查询后的完整结果为 `20261004-u250-clock-leaf-graph-093751-536323`。未使用中途输出作为最终容量依据。

本地轻量证据：

- [新容量表及来源](../../local-reports/20261004-u250-half-column-capacity/capacity/clock_capacity.json)
- [逐共享资源域容量表](../../local-reports/20261004-u250-half-column-capacity/capacity/half_column_capacities.tsv)
- [全片分析摘要](../../local-reports/20261004-u250-half-column-capacity/capacity/topology-summary.json)
- [软件参数实测](../../local-reports/20261004-u250-half-column-capacity/policy/clock_policy.tsv)
- [13 时钟结果](../../local-reports/20261004-u250-half-column-capacity/micro-13/result.tsv)、[16 时钟结果](../../local-reports/20261004-u250-half-column-capacity/micro-16/result.tsv)、[17 时钟失败日志](../../local-reports/20261004-u250-half-column-capacity/micro-17/console.log)
- [AMF 真实输入读取结果](../../local-reports/20261004-u250-half-column-capacity/inspection/inputs.json)
- [最终验证摘要](../../local-reports/20261004-u250-half-column-capacity/validation-summary.json)

完整大型连通图留在服务器；没有下载 DCP。

## 适用范围

本轮结果针对 U250 的 SLICE 专用叶时钟资源，包含 SLICEM LCLK 的端点可达性。还没有为 DSP/BRAM/URAM 单独建立所有时钟引脚的容量与共享约束，也没有扣除设计中已被复位、使能等全局网络占用的叶资源。任意复杂设计仍可能因局部连接、其他时钟网络层级、固定资源或路由选择而失败；名义容量 16 不保证所有 16 时钟组合都能布通。

CLK-FPGA08 原来的 X4Y6 **区域覆盖**超限是另一层约束，本次将半列名义容量读为 16 并不会自动修复它。本轮未重新运行该 benchmark 的完整布局布线。

官方交叉参考：[AMD UG949 的半列叶时钟结构](https://docs.amd.com/r/en-US/ug949-vivado-design-methodology/Clock-Routing-Root-and-Distribution)、[UG572 BUFCE_LEAF](https://docs.amd.com/r/en-US/ug572-ultrascale-clocking/BUFCE_LEAF-Clock-Buffer)、[UG835 get_nodes](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/get_nodes)、[ISPD 2017 的模型限制](https://www.ispd.cc/contests/17/legalization.html)。本文的 16 来自本次目标器件查询与实际路由证据，而非将公开模型常数推广到 U250。
