# GETRF 加速版 HPWL 分歧定位

> 2026-09-29 恢复记录：用户已结束新匹配内核实验，常规模式恢复 `legacy`，保留候选缓存／top-K。本文保留历史实验条件；当前状态见 [加速改动清单](placement-acceleration-inventory.md)。

2026-09-29。正式源码与实验位于 eda072 的 `/Projects/jinyang/workspace/AMFplacer3.0`。

四组同构建 AMF 对照已全部完成，最后一组于 2026-09-29 09:56:25（UTC+8）结束。**本次 GETRF 配置的 HPWL 翻倍由匹配求解器替换触发；候选缓存排序在两种求解器下均没有改变最终布局。** 结果按完整 HPWL 轨迹及导出布局 SHA256 核验，不能把此结论扩大为所有输入下缓存排序均等价。服务器原始汇总为 `experiments/evidence/20260929-placement-ablation/comparison.md`、`comparison.json`。

## 最终结果

| 组 | 求解器 / 候选排序 | 最终 HPWL | AMF 进程墙钟（分钟） | 最终布局 |
|---|---|---:|---:|---|
| A | legacy / legacy | 9,731,280.398641 | 86.43 | 与历史旧版完全一致 |
| B | component_assignment / legacy | 19,764,795.924150 | 33.86 | 与首次加速失败布局完全一致 |
| C | legacy / cached_topk | 9,731,280.398641 | 84.07 | 与 A 和历史旧版完全一致 |
| D | component_assignment / cached_topk | 19,764,795.924150 | 30.34 | 与 B 和首次加速失败布局完全一致 |

- A、C 的全部 366 条 HPWL 记录相同，导出布局 SHA256 均为 `075ceae495670c73604ed17bd068a0e1d7c43b9f7296228fecd4b6e73b0cda8a`。
- B、D 的全部 389 条 HPWL 记录相同，导出布局 SHA256 均为 `00c7d2f9358943909910f6d95a9419185ac28f156b4de8507411fc13584088d1`。
- 新求解器的最终 HPWL 为旧版的 2.031058 倍，增加约 103.11%。四组 AMF 退出码均为 0。
- 四组均经检查使用同一冻结二进制、相同输入哈希和相同控制参数，仅切换求解器及候选排序。
- 上表为并行运行下的 AMF 进程总墙钟，包含输入、内部计算和导出，未独立剥离纯 placement；不能据此计算公平的算法加速倍数。
- 本轮只运行 AMF，没有执行新的 Vivado 导入、placement、routing 或产生新 DCP。B、D 重现的是此前被 Vivado SLRC-1 拒绝的同一份导出布局，并非本轮重新执行了该 DRC。

## 同构建控制

四组均使用冻结构建 `build-20260929-012409-793918-c70df682`，AMFPlacer 二进制 SHA256：

`04ed4261b5ab6a5f9db9e472a96172139fb371f4b0313cc39c1d1275a7cbe80b`

输入网表、器件、物理模型与 GETRF post-opt DCP 相同；10 ns、8 线程、共享/SA 比例 0.71、物理边界延迟和二维聚拢均保持不变。所有组的 `VerifyMacroCandidateCosts=true`；旧候选模式不使用该复核分支。本轮并行运行，仅比较功能输出，不能用墙钟做隔离负载的速度结论。不执行 Vivado placement/routing。

| 组 | BipartiteMatchingBackend | MacroCandidateSelection | 运行 ID |
|---|---|---|---|
| A | legacy | legacy | getrf-u250-full-20260929-082959-766281 |
| B | component_assignment | legacy | getrf-u250-full-20260929-082959-766278 |
| C | legacy | cached_topk | getrf-u250-full-20260929-083116-517798 |
| D | component_assignment | cached_topk | getrf-u250-full-20260929-082959-766290 |

C 首次启动发生运行目录时间戳碰撞，在创建目录阶段退出，未启动 AMF；随后以独立目录重试。原始启动日志和重试记录保留，未修改其他组的运行目录。

## 第一处因果分歧

四组初始聚类、首轮 QP 与扩散的已记录结果相同。第一张匹配图也完全相同：

- SHA256：`ea1974dd207ae63f5b6de3c28fafc233c20357d473da5937867f1c80fd113a1a`
- 12,231 个左节点，10,775 个右节点，244,620 条候选边。
- 调用来自 `GlobalPlacer.cc:481` 附近的 `mCLBLegalizer->legalize()`，处理 SLICEM 类 PU。

执行匹配后，下一轮 QP 的 HPWL 首次分叉：

| 求解器 | HPWL after QP | pseudoNetWeight |
|---|---:|---:|
| legacy（A/C） | 1,908,135.375 | 0.003071 |
| component_assignment（B/D） | 1,908,116.250 | 0.003071 |

初始差值仅 19.125，新版在这一时点甚至略低；随后通过合法化目标、扩散、时序权重与聚拢的迭代反馈放大。不能把首次差异直接当成最终劣化幅度。

## 同一张图的独立回放

诊断程序从冻结构建的源码复制，未修改正式源码或运行中的二进制。保存所有匹配对后比较得到：

| 内核 | 匹配数 | 原始候选边总成本 |
|---|---:|---:|
| 原 legacy | 6,244 | 277,084.239667892 |
| component_assignment | 6,244 | 277,083.668979645 |
| component_spfa | 6,244 | 277,083.668979645 |
| component_ssp | 6,244 | 277,083.668979645 |

legacy 与 component_assignment 之间：

- 3,923 个左节点的匹配结果改变。
- 其中 3,655 个两边都匹配成功，但目标 site 不同。
- 134 个从未匹配变为匹配，另 134 个从匹配变为未匹配。
- 后一项会改变合法化后续扩大候选范围时处理的 PU 集合。CLBLegalizer 保留本轮已匹配对象，后续迭代处理剩余对象，因此改变不仅是最终 site 的交换。
- 总成本仅降低约 0.570688，不能据此推断完整 placement 的质量更好。

三个新内核的同图总成本相同，但 assignment 与 component_spfa 仍有 3,012 个对象匹配结果不同。这直接显示该候选图存在大量同总成本的不同解；局部标量成本不足以唯一决定布局轨迹。

## 对应代码改动

服务器源码文件均位于 `/Projects/jinyang/workspace/AMFplacer3.0/`：

1. `src/lib/HiFPlacer/placement/legalization/CLBLegalizer.cc:116`：第一处 SLICEM 匹配构造现在读取 `BipartiteMatchingBackend`；默认从旧实现切换为 `component_assignment`。同文件约 147 行还有另一处调用。
2. `src/lib/HiFPlacer/problemSolvers/MinCostBipartiteMatcher.cc:47`、`:49`：非 legacy 分支调用 `amf_matching::solve`，返回新的匹配向量。
3. `src/lib/HiFPlacer/problemSolvers/SparseMinCostMatching.h:197`：`solveAssignment` 改为逐行最短交替路径和对偶量求解。约 240 行按 `(距离, 空闲列优先, 列编号)` 决定堆顺序，与旧 SPFA 的遍历/同分顺序不同。
4. `src/lib/3rdParty/minCostFlow/MinCostFlow.h:68`、`:147`、`:209`：旧实现使用正向 `tcost + 0.01`、反向 `-tcost`，float 距离和 `eps=1e-5`。新实现没有沿用这组数值和搜索规则，因此不是仅改存储布局的等价加速。
5. `src/lib/HiFPlacer/placement/legalization/CLBLegalizer.cc:601` 的 `updateMatchingAndUnmatchedPUs()`：本轮匹配对象被保留，未匹配对象进入后续扩张，承接匹配结果差异。

宏候选排序的 `MacroLegalizer.cc:913`（`partial_sort`）确实会改变同分顺序，但 B 组在使用旧排序时已出现同样首处分歧。四组最终结果进一步确认：本次配置中 A=C、B=D，包括全部 HPWL 轨迹和导出布局哈希，因此该排序开关不是本轮最终 HPWL 回归的原因。

单独将旧 SPFA 改为 double，第一张图仍改变 84 个对象的选择，但未复现 assignment 的全部变化。直接去掉旧正向 0.01 的诊断副本触发旧内核的距离/入队次数断言；这些尝试不是有效的完整布局对照，不能据此把本轮回归唯一归因于 0.01。当前确证粒度是求解器替换整体，以及它产生的不同匹配与剩余对象集合。

## 证据和复核

- `experiments/evidence/20260929-placement-ablation/manifest.json`：四组命令、配置、PID、源码和二进制信息。
- 同目录 `comparison.json`、`comparison.md`：当前/最终的轨迹、配置一致性、布局哈希比较。
- 各组 `matching-graphs/`：前 12 次匹配的完整候选图，仅保留服务器。
- `experiments/preflight/20260929-placement-ablation-replay/results.json`、`assignment-differences.json`：同图成本、匹配变化与诊断变体。
- `scripts/diagnostics/analyze_placement_ablation.py`：重新生成比较结果。
- `scripts/diagnostics/finish_placement_ablation.py`：当前实验的报告收尾进程；只读取四组结果并更新报告，不启动新实验、不修改运行结果。

现阶段未修改正式 placement 实现或改变默认配置；本次新增内容仅为实验配置、诊断程序与证据。
