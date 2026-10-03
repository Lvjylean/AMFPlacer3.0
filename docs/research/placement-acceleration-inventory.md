# Placement 加速改动清单与旧求解器恢复

更新：2026-09-29。用户要求结束当前求解器实验，恢复旧求解器。
检查时两组 forward-bias 实验均已结束，服务器没有仍在运行的 AMFPlacer，也没有本轮待运行的结果收集任务；没有启动新一轮长时间布局实验。

## 当前生效选择

- 合法化二分图匹配使用 `BipartiteMatchingBackend=legacy`。
- 保留宏候选 `MacroCandidateSelection=cached_topk` 和只读网络列表引用优化。
- 旧内核原有正向 `c+0.01`、反向 `-c` 规则继续存在。新内核的 `BipartiteMatchingForwardBias` 不用于旧内核；无需为旧模式配置 `0.01`。
- 三种新内核及新内核扰动代码保留用于历史复现，常规配置不再启用。`placement-fast*`、`forward-bias-*` 与四组消融配置仍显式选择实验后端，不作为日常启动配置。
- 此处切换的是 **CLB／宏合法化的二分图匹配求解器**；QP 数值求解器没有在这批加速中替换。

恢复涉及 `MinCostBipartiteMatcher.h` 的默认参数以及 `CLBLegalizer.cc`、`MacroLegalizer.cc` 共四处调用的缺省值，共五处均设为 `legacy`。八个此前未显式指定后端的常规 GETRF 完整布局配置补上 `legacy`，其他设计／时序参数保持原值。

新增明确的稳定配置：

`/Projects/jinyang/workspace/AMFplacer3.0/configs/experiments/getrf-u250-placement-stable.json`

该配置为 GETRF/U250、10 ns、8 线程、共享与 SA 比例 0.71、物理边界模型及二维聚拢，使用旧匹配内核、候选成本缓存和 top-K，并开启成本复核。它对应此前已完成的 C 组模式。

后续如需再次运行这一已验证组合，可使用：

```sh
cd /Projects/jinyang/workspace/AMFplacer3.0
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-placement-stable.json \
  --binary builds/build-20260929-012409-793918-c70df682/build/AMFPlacer \
  --amf-only
```

以上命令在本次恢复中没有启动。去掉 `--amf-only` 才会继续 Vivado 导入、placement 与 routing。按 2026-09-30 用户决定，完整后端默认允许修复；需要复现严格导入关卡时增加 `--strict-import`。
`getrf-u250-sa-calibrated.json` 也已显式配置旧匹配内核和缓存候选选择。

## 实际实施的加速改动

核对边界是已上传基线 `33d881d0`（标签 `多die_修正比例_修正延迟_修正扩散策略`）到加速提交 `c70df682`，加上随后正向扰动实验与本次恢复。器件坐标／延迟系数的其他开发不归入本清单。

| 类别 | 具体改动 | 对逻辑／结果的影响 | 当前状态 |
|---|---|---|---|
| 匹配图组织与内存 | 在真实候选图上找连通分量，局部重编号，按规模动态分配 OpenMP 任务；新残量图使用连续边和反向下标，减少逐边分配、全图工作数组与重复清零 | 没有删除跨 SLR/CR 候选边，但任务拆分和搜索／同分顺序可能改变输出 | 随新内核停用；legacy 沿用旧图组织 |
| 匹配求解算法 | 增加 `component_spfa`、势函数 Dijkstra 的 `component_ssp`、稀疏交替路径的 `component_assignment`；后者使用行／列对偶量和私有虚拟列处理不足匹配 | 新实现使用 double，改变增广顺序、同成本解选择和部分匹配对象；并非旧代码的完全等价加速 | 停用新内核，恢复 legacy |
| 宏候选成本缓存 | 每轮每个候选只算一次 `getHPWLChange()`，保存 `{cost, 原候选序号}`；选中成本复用到紧接的建图，下一轮重建缓存 | 不跨迭代缓存；提供重新计算所选成本的验证开关 | 保留 |
| 宏候选 top-K | 用 `partial_sort` 选择前 K 个，替代全部 quicksort 后截断；同成本按原候选搜索顺序处理 | K、搜索范围和合法性过滤不变；同成本次序理论上可能变化，本次 GETRF 完整对照结果一致 | 保留 |
| 避免只读列表复制 | `MacroLegalizer.h` 中三处 PU 网络列表从按值复制改为 `const auto &` | 保持遍历内容和代价公式，减少临时分配与复制 | 保留 |
| 新内核不对称扰动试验 | 增加 `BipartiteMatchingForwardBias`，可令正向为 `c+b`、反向为 `-c`；指派后端同步调整反向搜索和对偶更新 | 明确改变增广路径选择；0.01 试验未改善最终 HPWL | 保留试验代码，常规流程不用 |

前三种新内核共用的真实连通分量划分、局部编号和并行调度没有单独移植进 legacy。本次恢复旧求解器后，不能继续声称这些图层面的加速仍在日常旧模式中生效。

## 辅助改动

- 匹配日志：记录后端、左右结点数、边数、分量规模、匹配数量、原成本、准备／求解时间；扰动实验另记录实际 bias。
- 候选图快照：`AMF_MATCHER_DUMP_DIR`、最小图规模和最大文件数，便于在同一输入上独立重放；未设置时不采集。
- 独立 `checkBipartiteMatching` 目标及 6,104 组小图验证，包含精确最优值／匹配数量、重复边、不足匹配、无效输入、线程确定性；后续增加不对称扰动用例与重放参数。sanitizer 验证保留。
- 新增快模式、成本复核、旧模式、四组消融、两组扰动等独立配置。
- 新增消融分析与有限时长结果收集脚本，记录同一构建、输入哈希、完整 HPWL 轨迹和最终布局 SHA256。
- 加速前已完成的 RuntimeProfiler 属于定位瓶颈的准备工作；不是这批算法加速本身。未将带插桩的历史布局耗时与普通构建进程总墙钟直接计算公平加速倍数。

## 效果与保留依据

四组同构建对照使用冻结二进制 `build-20260929-012409-793918-c70df682`。

| 模式 | 最终 HPWL | 完整布局比较 |
|---|---:|---|
| 旧求解器 + 旧候选排序 | 9,731,280.398641 | 历史布局参考 |
| 旧求解器 + 缓存／top-K | 9,731,280.398641 | 366 条 HPWL 轨迹和导出布局哈希均与上一行一致 |
| 新求解器 + 旧候选排序 | 19,764,795.924150 | 与新求解器缓存模式一致 |
| 新求解器 + 缓存／top-K | 19,764,795.924150 | 相对旧求解器增加 103.11% |
| 新求解器 + 缓存／top-K + 不对称 0.01 | 27,384,113.397561 | 另一次同构建两组实验；相对无扰动新求解器增加 38.55% |

最后一行所属两组实验的零扰动控制复现了新求解器全部 389 条轨迹与最终布局哈希。仅补回扰动不足以恢复旧布局质量。

缓存／top-K 的并行运行观测：旧求解器模式进程墙钟 5,185.640→5,044.264 秒，少 141.376 秒（2.36 分钟，2.73%）；新求解器模式 2,031.772→1,820.358 秒，少 211.414 秒（3.52 分钟，10.41%）。这些是共享服务器上的观测，不是隔离负载的速度定论；两类改动也没有分开测量各自收益。

新求解器局部匹配非常快，但原快模式完整流程在 Vivado place_design 前的 SLR 容量检查失败，routing 未开始。不能将局部匹配加速或 AMF 正常退出作为全流程验收通过。缓存结果一致的结论限定于本次 GETRF 条件，不推广到所有设计。

## 主要代码位置

以下均相对服务器根目录 `/Projects/jinyang/workspace/AMFplacer3.0`：

| 文件 | 作用 |
|---|---|
| `src/lib/HiFPlacer/problemSolvers/SparseMinCostMatching.h` | 新分量、残量网络、稀疏指派与扰动实现 |
| `src/lib/HiFPlacer/problemSolvers/MinCostBipartiteMatcher.h` / `.cc` | 后端开关、默认 legacy、保留旧实现、图快照与计时 |
| `src/lib/HiFPlacer/placement/legalization/CLBLegalizer.cc` | CLB 两处匹配调用接线 |
| `src/lib/HiFPlacer/placement/legalization/MacroLegalizer.cc` / `.h` | 宏两处匹配调用、候选缓存、top-K、引用优化 |
| `src/tests/check_bipartite_matching.cc` | 单元验证、合成图及真实图重放 |
| `src/CMakeLists.txt` | 新增匹配测试目标；加速提交未改变编译优化级别 |
| `scripts/diagnostics/analyze_placement_ablation.py`、`finish_placement_ablation.py`、`analyze_forward_bias.py` | 实验分析和结果汇总 |

旧算法 `src/lib/3rdParty/minCostFlow/MinCostFlow.h` 没有在这批加速中改写。

## 尚未实施的建议

网格增量维护／重复刷新消除、扩散线程池与对象池、增量 STA、引脚坐标增量更新、区域用量增量维护、审计频率削减、QP 求解器替换，均没有作为这批加速落地。QP 目标与迭代数、SA、扩散规则、打包策略、候选数量与搜索半径、SLR 合法性及容量建模也没有因本次加速而修改。

## 恢复验收与构建说明

恢复证据：`experiments/evidence/20260929-restore-legacy-matcher/`，包含修改前文件、前后哈希与验证记录。
独立验证构建：`builds/build-20260929-111524-089457-c70df682`；该构建保留服务器当时其他工作区源码，不自动晋升为完整 QoR 基线。
编译通过，五处冻结源码默认值均核验为 legacy。第一张 GETRF 实际候选图以旧后端重放：匹配 6,244 个对象，原成本 277084.239667892，指派哈希 `11227901041438737952`，三项均与旧参考一致。
`builds/current` 保持原指向 `build-20260928-002048-349585-d534237b/build`，其本身使用旧求解器；为保留已验证的较新代码与缓存优化，稳定配置的重现实验命令显式指定上述 012409 冻结构建。

本地只同步配置、文档和轻量证据。`getrf-u250-sa-calibrated.json` 检测到两端其他字段已有差异，本次只合并本地的后端／候选选择字段，保留两端其他修改。正式运行以服务器配置为准。
