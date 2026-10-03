# 新求解器保留不对称正向扰动

> 2026-09-29 恢复记录：用户已结束新匹配内核实验，常规模式恢复 `legacy`，保留候选缓存／top-K。本文保留历史实验条件；当前状态见 [加速改动清单](placement-acceleration-inventory.md)。

2026-09-29，用户要求在新匹配求解器中保留正向边 `c + 0.01`、反向边 `-c`，重新运行 GETRF 比较最终 HPWL。

## 修改与语义

新增实验参数 `BipartiteMatchingForwardBias`，默认 `0`；本轮处理组显式设为字符串 `"0.01"`。
该参数接入 CLB/SLICEM 与宏合法化使用的二分图匹配，不修改 QP、SA、候选排序或物理延迟模型。
`legacy` 保持原内置扰动，拒绝另设非零参数，以免误解为在旧扰动上叠加。

新默认后端 `component_assignment` 没有显式存储所有残量边，因而不能只在候选成本上统一加常数。
本次同时调整搜索和对偶量更新：

\[
r_{\rm forward}=c+b-u_i-v_j,\qquad
r_{\rm reverse}=u_i+v_j-c.
\]

当 `b=0` 时保留原有匹配反向边零缩减成本的计算路径；当 `b>0` 时显式计算反向缩减成本，允许其为正。
经过已匹配列到行的搜索距离加上这个反向成本，对偶更新使用该行实际距离，并对超出终点距离的行使用零增量。
候选原成本及日志中的匹配成本仍为未加扰动的成本和。

若一条增广路径经过 `k` 条反向候选边，则会经过 `k+1` 条正向候选边，额外候选边成本为 `(k+1)b`。
公共源、汇正向弧为每条增广路径增加同样的 `2b`，不改变它们之间的选择。
因此每额外撤销一条旧匹配，相比直接增广，多承担 `b`；并非对最终全部已选边简单加常数。

这保留的是旧实现的“不抵消扰动”规则。新实现的逐行增广顺序、连通分量拆分、双精度和同成本选择仍然保留，不能据此保证复现旧求解器布局。
正向扰动是路径选择启发式；启用后不宣称严格最小化原始候选成本。最大匹配数量仍优先。

## 修改文件（服务器根目录下）

根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

- `src/lib/HiFPlacer/problemSolvers/SparseMinCostMatching.h`：残量成本和隐式指派搜索。
- `src/lib/HiFPlacer/problemSolvers/MinCostBipartiteMatcher.h`、`.cc`：参数、校验和记录实际扰动值。
- `src/lib/HiFPlacer/placement/legalization/CLBLegalizer.cc`、`MacroLegalizer.cc`：四处构图调用传参。
- `src/tests/check_bipartite_matching.cc`：不对称扰动用例、零扰动 oracle、匹配数量和并行确定性；重放命令支持可选 bias。

## 验证

- 6,104 组小图测试通过：三种新后端分别运行 `0` 与 `0.01`；零扰动检查精确最优值，正扰动检查匹配数量与小图成本范围。
- 专门检查单次撤销、两次撤销，以及收益超过扰动时仍允许撤销的情况。
- 1/8 线程匹配结果一致。
- AddressSanitizer 与 UndefinedBehaviorSanitizer 通过同一测试集。
- 三张 GETRF 实际候选图重放均通过；零扰动复现之前的指派哈希。
- 冻结构建与先前构建相比仅上述六个源码文件变化；与预检使用的源码逐字节一致。

第一张候选图：12,231 个左结点、10,775 个右结点、244,620 条边；两种扰动均匹配 6,244 个 PU。

| 扰动 | 匹配原成本 | 指派哈希 |
|---|---:|---|
| 0 | 277083.668979645 | 4086328591127448218 |
| 0.01 | 277084.699831009 | 1508470353096224818 |

这些数是局部合法化候选成本，不是最终 HPWL。
预检、编译日志、sanitizer 结果和实际图重放证据：`experiments/preflight/20260929-forward-bias/`。

## 完整 AMF 对照实验

共同条件：GETRF/U250，10 ns，8 线程，共享与 SA 的纵横比例均为 0.71，物理边界模型和二维聚拢开启，`cached_topk` 与成本复核开启。
使用原始 U250 坐标及 `u250-physical-v1` 输入；没有混入其他进行中的器件坐标/延迟标定。
两组配置只有 `BipartiteMatchingForwardBias` 不同。

冻结源码提交基底：`c70df682d8b4bdbe9b71b56008d1087d52ddad51` 加构建中保存的工作区补丁。
冻结构建：`builds/build-20260929-103634-559623-c70df682`。

| 组 | 配置 | 运行 ID |
|---|---|---|
| 新求解器，扰动 0 | `configs/experiments/getrf-u250-forward-bias-control.json` | `getrf-u250-full-20260929-103919-993195` |
| 新求解器，扰动 0.01 | `configs/experiments/getrf-u250-forward-bias-001.json` | `getrf-u250-full-20260929-103921-994354` |

启动方式：

```sh
cd /Projects/jinyang/workspace/AMFplacer3.0
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-forward-bias-001.json \
  --binary builds/build-20260929-103634-559623-c70df682/build/AMFPlacer \
  --amf-only
```

控制组将配置替换为 `getrf-u250-forward-bias-control.json`。

历史相同输入、参数的参考：旧求解器 HPWL **9,731,280.398641**；无扰动新求解器 HPWL **19,764,795.924150**。
实验目录均位于 `experiments/runs/`，证据清单与持续更新的比较结果位于 `experiments/evidence/20260929-forward-bias/{manifest,comparison}.json`。
比较脚本：`scripts/diagnostics/analyze_forward_bias.py`。它检查同一二进制、受控配置、输入哈希、实际生效扰动、HPWL 轨迹和最终布局导出哈希。

当前仅运行 AMF，没有运行 Vivado placement/routing，也没有生成新的最终 DCP。
并发进程墙钟不是隔离负载速度比较；AMF 进程时间包含输入读取和输出适配，不记为纯布局算法时间。
本轮最终 HPWL 以 `comparison.json` 中状态为 `completed` 的 `final_hpwl` 为准；运行中的轨迹不冒充最终结果。

## 完成结果

两组已于 2026-09-29 正常完成完整 AMF 布局，退出码均为 0。**加入不对称 0.01 扰动后，最终 HPWL 增加 38.55%，本次没有改善。**

| 实验 | 最终 HPWL | 相对无扰动新求解器 |
|---|---:|---:|
| 历史旧求解器参考 | 9,731,280.398641 | — |
| 新求解器，扰动 0 | 19,764,795.924150 | 基准 |
| 新求解器，正向 +0.01、反向不抵消 | 27,384,113.397561 | +38.5499% |

- 扰动组相对旧求解器参考增加 181.40%。单独恢复这项扰动不足以恢复旧求解器的 GETRF 布局结果。不能据此单独判断剩余差异中哪一项是主因。
- 控制组全部 389 条 HPWL 轨迹与原新求解器相同，最终导出布局哈希也相同，验证了零扰动兼容性。
- 处理组 420 条 HPWL 记录；首个分叉出现在第 11 条（下一次 QP）：控制组 1,908,116.25，处理组 1,908,112.875，随后差异逐步放大。
- 匹配调用：控制组 290 次、累计 1.245036 秒；处理组 323 次、累计 1.347051 秒。

| 耗时口径 | 扰动 0 | 扰动 0.01 |
|---|---:|---:|
| AMF 进程墙钟（含读取/导出适配） | 1366.838760 s（22.78 min） | 1676.193821 s（27.94 min） |
| 纯布局算法时间 / 文件适配独立时间 | 本轮未单独计时 | 本轮未单独计时 |
| Vivado placement / routing | 未执行 | 未执行 |

两组并发运行；处理组在控制组结束后独占了部分后续时间，因此进程墙钟不用于严格的速度因果比较。

最终布局 TCL 位于上述各运行目录的 `placement/DumpCLBPacking-first-0.tcl`。此轮没有产生最终 DCP。

布局文件 SHA256：

- 扰动 0：`00c7d2f9358943909910f6d95a9419185ac28f156b4de8507411fc13584088d1`。
- 扰动 0.01：`ca1acff4249277b52fa8a916f961055ab92bb1862b18eaa840026d8faaf8e6ef`。

本地轻量证据镜像：`local-reports/forward-bias-20260929/`。配置保留为独立实验配置，未将 0.01 改成全局默认。
