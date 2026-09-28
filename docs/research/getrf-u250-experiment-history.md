# GETRF / U250 历次实验记录

数据核对时间：2026-09-28T18:21:56.088468+08:00。服务器：`eda072`（jinyang）；根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

共 36 条正式运行目录记录，其中 8 轮生成最终 routed DCP。R01 的 AMF 导出不完整，只作诊断；其余 7 轮通过完整导出与实现验收，其中 3 轮满足 setup 时序。另有严格导入验证、失败/主动停止和辅助标定，逐条列于后文。范围为 `experiments/runs/` 内 GETRF 相关记录；单元测试、微小器件预检和不生成新布局的报告分析不另算完整实验。

## 口径与可比性

- 8 轮最终结果均使用原始 GETRF post-opt DCP，SHA256 `6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`；器件 `xcu250-figd2104-2L-e`，Vivado 2024.2。已逐轮核对 AMF 目标 10 ns 及 Vivado `ap_clk=10.000 ns`。这是 OOC 内核实验，不是完整板级实现。
- AMF 耗时来自 `manifest.json` 的 AMF 进程 elapsed_seconds，包含 AMF 内部处理、布局、打包、导出与退出；placement/routing 分别取 Vivado `place_design`/`route_design` 的完整调用耗时，routing 包含其内部迭代和重布线。
- 总墙钟统一为 `status.finished − manifest.started`。含 AMF、布局导入、Vivado 布局布线、审计、报告、DCP 写出、部分轮次的边界时序抽样以及外围处理；**不含初始综合/post-opt、首次 DCP→AMF 网表导出、源码构建和开发时间**。本批均复用导出缓存。
- “其他合计”＝总墙钟−AMF−placement−routing，已包含导入，不能再次加上导入。它还含审计/报告等实验开销，不等同于工具适配的纯必要成本。各行独立四舍五入可能有 0.01 分钟差异。旧三组比较文档从更外层启动器计时，比本表多约 0.46 秒；本表统一采用运行目录自身时间戳。
- `—` 表示未执行、未完成或没有可靠记录，不能当作 0。停止时尚未结束的阶段不估算完整耗时。阶段失败的 AMF/工具时间是直到失败/停止的已记录时间。
- 历史实验存在并行和共享服务器负载；不能将跨构建、跨轮次差异都归因于单个参数。R03–R05 同一冻结二进制、三组并行；R07–R08 同一二进制，SA 均 0.71，共享比例由 0.4 改为 0.71。历史配置以每轮快照为准，不能用当前配置反推。
- 本次只核验 DCP 路径及文件大小；CSV 中 SHA256 为当时 status 记录，未重新读取全部 DCP 计算哈希。DCP 只保存在服务器。

## 早期 U250 适配（09-26）

R01 漏导出 389 个 MUX 的 BEL，后端完成布线不能替代 AMF 完整性验收。R02 是首次完整 U250 基线，尚未包含后来加入的 SLR 边界延迟。

| 指标 | R01 早期导出不完整诊断 | R02 首次完整 U250 基线 |
|---|---:|---:|
| 开始时间（UTC+8） | 09-26 17:31 | 09-26 18:37 |
| WNS（ns） | −2.872 | −2.886 |
| TNS（ns） | −1376.263 | −811.808 |
| setup 违例端点 | 2520 | 1744 |
| AMF 耗时（分钟） | 52.10 | 56.85 |
| Vivado placement（分钟） | 17.77 | 15.68 |
| Vivado routing（分钟） | 80.38 | 119.40 |
| 总墙钟（分钟） | 187.52 | 229.68 |
| 其中：AMF 布局导入（分钟） | 28.50 | 28.69 |
| AMF/placement/routing 之外合计（分钟，含导入） | 37.26 | 37.76 |
| WHS（ns） | +0.019 | +0.019 |
| hold 违例端点 | 0 | 0 |
| 共享 / SA 有效比例 | 0.4 / 0.32 | 0.4 / 0.32 |
| 原 LOC/BEL 保留率 | 99.7887% | 99.7855% |
| 严格导入验收 | 旧流程，允许后端修复 | 旧流程，允许后端修复 |
| AMF 完整性＋实现验收 | 未通过 | 通过 |

本表最终 DCP（均在 eda072）：

- **R01 早期导出不完整诊断**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260926-173107-822886/reports/getrf_routed.dcp`
- **R02 首次完整 U250 基线**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260926-183758-400040/reports/getrf_routed.dcp`

## 物理边界三组对照（09-27，严格导入修复前）

R03 control 使用旧 X 方向经验修正＋SLR 1.5 ns；R04 delay 使用实际物理边界模型；R05 cluster 在 R04 上增加容量感知二维聚拢。三组共同使用时序权重上限与 QP 数值保护。三组当时均允许 Vivado 修复导入拒绝的位置，不能与严格导入版混为同一阶段。

| 指标 | R03 control：旧 X 修正＋SLR 惩罚 | R04 delay：物理边界延迟 | R05 cluster：边界延迟＋二维聚拢 |
|---|---:|---:|---:|
| 开始时间（UTC+8） | 09-27 06:35 | 09-27 06:35 | 09-27 06:35 |
| WNS（ns） | −0.137 | +0.003 | −0.059 |
| TNS（ns） | −1.708 | 0 | −0.072 |
| setup 违例端点 | 36 | 0 | 2 |
| AMF 耗时（分钟） | 92.19 | 91.12 | 111.81 |
| Vivado placement（分钟） | 18.19 | 15.74 | 16.36 |
| Vivado routing（分钟） | 59.14 | 79.38 | 113.47 |
| 总墙钟（分钟） | 215.68 | 229.85 | 285.20 |
| 其中：AMF 布局导入（分钟） | 29.08 | 29.18 | 30.76 |
| AMF/placement/routing 之外合计（分钟，含导入） | 46.16 | 43.60 | 43.56 |
| WHS（ns） | +0.021 | +0.019 | +0.023 |
| hold 违例端点 | 0 | 0 | 0 |
| 共享 / SA 有效比例 | 0.4 / 0.32 | 0.4 / 0.32 | 0.4 / 0.32 |
| 原 LOC/BEL 保留率 | 99.8028% | 99.7923% | 99.7819% |
| 严格导入验收 | 旧流程，允许后端修复 | 旧流程，允许后端修复 | 旧流程，允许后端修复 |
| AMF 完整性＋实现验收 | 通过 | 通过 | 通过 |

本表最终 DCP（均在 eda072）：

- **R03 control：旧 X 修正＋SLR 惩罚**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-912642/reports/getrf_routed.dcp`
- **R04 delay：物理边界延迟**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-913333/reports/getrf_routed.dcp`
- **R05 cluster：边界延迟＋二维聚拢**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-912759/reports/getrf_routed.dcp`

## 严格导入修复与横纵比例实验（09-27 至 09-28）

R06–R08 均为 delay 模式，二维聚拢关闭，严格导入及最终原 LOC/BEL 保留率均为 100%。R06 和 R07 还存在既有 CLB 打包稳健性代码差异；R07→R08 保持同一二进制与 SA=0.71，改动共享 y2xRatio。R06 的 WNS 余量仍优于 R08；R08 是本次历史记录中总墙钟最短的完整通过轮次。

| 指标 | R06 严格导入修复后 delay | R07 仅 SA 比例 0.71 | R08 共享及 SA 比例均为 0.71 |
|---|---:|---:|---:|
| 开始时间（UTC+8） | 09-27 12:50 | 09-28 01:40 | 09-28 16:02 |
| WNS（ns） | +0.111 | −0.353 | +0.019 |
| TNS（ns） | 0 | −2.896 | 0 |
| setup 违例端点 | 0 | 26 | 0 |
| AMF 耗时（分钟） | 68.57 | 51.16 | 53.82 |
| Vivado placement（分钟） | 3.65 | 2.88 | 2.90 |
| Vivado routing（分钟） | 48.13 | 45.65 | 25.52 |
| 总墙钟（分钟） | 147.89 | 123.88 | 106.06 |
| 其中：AMF 布局导入（分钟） | 12.29 | 10.21 | 10.20 |
| AMF/placement/routing 之外合计（分钟，含导入） | 27.54 | 24.19 | 23.82 |
| WHS（ns） | +0.023 | +0.019 | +0.023 |
| hold 违例端点 | 0 | 0 | 0 |
| 共享 / SA 有效比例 | 0.4 / 0.32 | 0.4 / 0.71 | 0.71 / 0.71 |
| 原 LOC/BEL 保留率 | 100.0000% | 100.0000% | 100.0000% |
| 严格导入验收 | 通过 | 通过 | 通过 |
| AMF 完整性＋实现验收 | 通过 | 通过 | 通过 |

本表最终 DCP（均在 eda072）：

- **R06 严格导入修复后 delay**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-125004-730657/reports/getrf_routed.dcp`
- **R07 仅 SA 比例 0.71**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-014049-967020/reports/getrf_routed.dcp`
- **R08 共享及 SA 比例均为 0.71**：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-160231-470820/reports/getrf_routed.dcp`


## 其余运行逐条记录

以下均没有最终 routed DCP，因此不填写最终 WNS/TNS。记录为 completed 的 import-only/packing/标定轮次仅代表其限定任务完成。原始状态为 failed 但备注写主动停止的，按历史退出状态保留，不改写成算法失败。时间单位为分钟。

| 运行 ID | 模式/范围 | 原始状态 | AMF | Vivado 进程 | 布局导入 | 总墙钟 | 结果/原因 |
|---|---|---|---:|---:|---:|---:|---|
| `getrf-u250-resources-20260926-143230-527023` | 早期适配/专项验证 / auxiliary | failed | — | 1.90 | — | 1.90 | 硬资源导入后，审计未处理 URAM BEL 的类型前缀而失败；不是资源放置错误的证据。 |
| `getrf-u250-resources-20260926-143933-202523` | 早期适配/专项验证 / auxiliary | completed | — | 2.99 | — | 2.99 | 硬资源验证通过，26096 个单元 LOC/SLR 匹配；仅部分资源 DCP。 |
| `getrf-u250-full-20260926-150155-081308` | 早期适配/专项验证 / incomplete | failed | 2.90 | — | — | 2.90 | 无固定 I/O 种子，聚类初始化不退出，主动停止。 |
| `getrf-u250-full-20260926-150648-975751` | 早期适配/专项验证 / incomplete | failed | 7.04 | — | — | 7.04 | SRL 资源分类/密度网格断言失败。 |
| `getrf-u250-packing-20260926-151115-924561` | 早期适配/专项验证 / auxiliary | completed | 0.59 | — | — | — | 仅初始打包：198 个 SRL/MUX 宏、594 个真实单元；非完整 placement。 |
| `getrf-u250-srl-audit-20260926-151231-308935` | 早期适配/专项验证 / auxiliary | completed | — | 1.49 | — | — | Vivado 局部 SRL/MUX 验证：594/594 LOC/BEL 全匹配；未写出最终 DCP。 |
| `getrf-u250-full-20260926-151909-521419` | 早期适配/专项验证 / incomplete | failed | 51.30 | — | — | 51.30 | 完成布局打包，但 DumpCLBPacking 路径重复拼接导致导出失败。 |
| `getrf-u250-full-20260926-161904-089708` | 早期适配/专项验证 / incomplete | failed | 15.48 | — | — | 15.48 | DirectMacroLegalize 导致大幅反复位移，主动停止。 |
| `getrf-u250-full-20260926-163444-325508` | 早期适配/专项验证 / incomplete | failed | 21.13 | — | — | 21.13 | 分数列预算取整为 0，合法化无进展，主动停止。 |
| `getrf-u250-full-20260926-170252-047980` | 早期适配/专项验证 / incomplete | failed | 26.40 | — | — | 26.40 | 减少前期迭代至 9 后末段 QP 发散，出现 NaN，退出 2。 |
| `getrf-u250-route-altclb-20260926-213914-179421` | 早期适配/专项验证 / auxiliary | stopped | — | 51.22 | — | 51.22 | 复用已布局 DCP，尝试 AlternateCLBRouting；默认流程成功后主动停止，未完成路由。 |
| `getrf-u250-full-20260927-030230-487935` | cluster / incomplete | cancelled | — | — | — | 5.29 | RAMB36 与虚拟 RAMB18 占位重复计数，三组主动停止并统一修正。 |
| `getrf-u250-full-20260927-030230-490881` | control / incomplete | cancelled | — | — | — | 5.29 | RAMB36 与虚拟 RAMB18 占位重复计数，三组主动停止并统一修正。 |
| `getrf-u250-full-20260927-030230-491096` | delay / incomplete | cancelled | — | — | — | 5.29 | RAMB36 与虚拟 RAMB18 占位重复计数，三组主动停止并统一修正。 |
| `getrf-u250-full-20260927-032031-594456` | delay / incomplete | cancelled | — | — | — | 39.62 | control 出现 NaN 后，停止本组，以相同数值保护重新运行。 |
| `getrf-u250-full-20260927-032031-595260` | cluster / incomplete | cancelled | — | — | — | 39.62 | control 出现 NaN 后，停止本组，以相同数值保护重新运行。 |
| `getrf-u250-full-20260927-032031-594482` | control / incomplete | failed | 33.43 | — | — | 33.43 | 时序权重过大导致 QP 坐标 NaN，control 失败；另两组随之停止统一修复。 |
| `getrf-u250-full-20260927-041344-552357` | control / incomplete | cancelled | — | — | — | — | 首轮 QP 装配性能问题，主动停止改为一次性稀疏装配；缺少结束时间。 |
| `getrf-u250-full-20260927-041344-557157` | cluster / incomplete | cancelled | — | — | — | — | 首轮 QP 装配性能问题，主动停止改为一次性稀疏装配；缺少结束时间。 |
| `getrf-u250-full-20260927-041344-564377` | delay / incomplete | cancelled | — | — | — | — | 首轮 QP 装配性能问题，主动停止改为一次性稀疏装配；缺少结束时间。 |
| `getrf-u250-full-20260927-041854-078213` | control / incomplete | failed | 90.99 | 48.01 | 34.13 | 139.78 | AMF 已完成；为统一构建重跑三组，主动停止旧后端并释放资源。 |
| `getrf-u250-full-20260927-041854-079412` | cluster / incomplete | failed | 136.08 | — | — | 136.08 | 3774 个 PU 打包停滞；定位遗留硬时钟列筛选后主动停止。 |
| `getrf-u250-full-20260927-041854-083355` | delay / incomplete | failed | 90.17 | 48.82 | 33.11 | 139.78 | AMF 已完成；为统一构建重跑三组，主动停止旧后端并释放资源。 |
| `getrf-u250-packing-20260927-124630-816660` | delay / auxiliary | completed | 0.55 | — | — | — | 仅初始打包：CLB 修复后的完整输入检查；非完整 placement。 |
| `getrf-u250-full-20260927-125907-199280` | control / incomplete | failed | 98.73 | — | — | 98.73 | control 导入验证的 AMF 阶段失败，256 个 PU 因时钟列搜索限制未完成打包。 |
| `getrf-u250-full-20260927-125913-760094` | cluster / import-only | completed | 80.70 | 14.73 | 11.83 | 96.23 | cluster 完成 AMF 与严格导入，856998 个原 LOC/BEL 全匹配；未运行 placement/routing。 |
| `getrf-u250-full-20260927-144536-634632` | control / import-only | completed | 60.88 | 12.49 | 10.20 | 74.01 | 修复跨列搜索回退后，control 完成 AMF 与严格导入；未运行 placement/routing。 |
| `u250-sa-ratio-calibration-20260928-005532` | delay / auxiliary | completed | — | — | — | — | SA 距离比例标定/归档记录，0.32→0.71；不是全流程，归档时间不代表采样总耗时。 |

上表中的 category：import-only＝仅严格导入；incomplete＝计划流程未完成；auxiliary＝硬资源、初始打包、局部验证、备用路由或标定。packing-only 缺少结束时间，保留 AMF 进程计时，不将其冒充总墙钟。

上表仅有以下部分资源 DCP，**不代表完整布局布线**：

- `/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-resources-20260926-143933-202523/reports/getrf_hard_resources_partial.dcp`

## 构建和原始证据

下面的启动提交是 manifest 中的 source_commit；实际运行可能包含未提交修改，应结合冻结二进制和 inputs/build_manifest.json、working_tree.patch 追溯。CSV 还保存完整二进制哈希、输入哈希、最终 DCP 记录哈希及退出状态。

| 轮次 | 运行 ID | 启动提交 | 冻结构建目录 |
|---|---|---|---|
| R01 | `getrf-u250-full-20260926-173107-822886` | `fbebbe04` | `build-20260926-165756-043079-fbebbe04/build/AMFPlacer` |
| R02 | `getrf-u250-full-20260926-183758-400040` | `fbebbe04` | `build-20260926-183632-669176-fbebbe04/build/AMFPlacer` |
| R03 | `getrf-u250-full-20260927-063459-912642` | `49ac3e58` | `build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer` |
| R04 | `getrf-u250-full-20260927-063459-913333` | `49ac3e58` | `build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer` |
| R05 | `getrf-u250-full-20260927-063459-912759` | `49ac3e58` | `build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer` |
| R06 | `getrf-u250-full-20260927-125004-730657` | `1f496daf` | `build-20260927-124444-562943-a2a94a48/build/AMFPlacer` |
| R07 | `getrf-u250-full-20260928-014049-967020` | `d534237b` | `build-20260928-002048-349585-d534237b/build/AMFPlacer` |
| R08 | `getrf-u250-full-20260928-160231-470820` | `d534237b` | `build-20260928-002048-349585-d534237b/build/AMFPlacer` |

每轮原始证据位于服务器对应运行目录：`manifest.json`、`status.json`、`config.json`、`reports/summary.json`（若完成）、`reports/stages.tsv`、`reports/timing_summary.rpt`。本表的 8 轮时序已与原始 timing_summary 的 WNS/TNS/端点数交叉核验，阶段计时已与 stages.tsv 交叉核验。

每轮还保留以下 **placement 中间 DCP**，不作为本表最终时序结果：

- R01：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260926-173107-822886/reports/getrf_placed.dcp`
- R02：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260926-183758-400040/reports/getrf_placed.dcp`
- R03：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-912642/reports/getrf_placed.dcp`
- R04：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-913333/reports/getrf_placed.dcp`
- R05：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-063459-912759/reports/getrf_placed.dcp`
- R06：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260927-125004-730657/reports/getrf_placed.dcp`
- R07：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-014049-967020/reports/getrf_placed.dcp`
- R08：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-160231-470820/reports/getrf_placed.dcp`

相关历史说明：[全流程适配](u250-getrf-full-flow.md)、[物理边界三组验收](device-physical-boundary-getrf-validation.md)、[严格导入修复](clb-import-legality.md)、[SA 0.71 初次 10 ns 实验](u250-sa-ratio-10ns-validation.md)。

配套 CSV：[逐轮原始数值与路径](getrf-u250-experiment-history.csv)。库存快照和验证哈希在服务器 `experiments/evidence/20260928-getrf-experiment-history/inventory.json`；本地轻量副本在 `local-reports/getrf-u250-history-20260928/inventory.json`。
