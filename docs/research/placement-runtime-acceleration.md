# Placement 第一批加速：匹配与候选选择

> 2026-09-29 恢复记录：用户已结束新匹配内核实验，常规模式恢复 `legacy`，保留候选缓存／top-K。本文保留历史实验条件；当前状态见 [加速改动清单](placement-acceleration-inventory.md)。

基准版本：`33d881d0`，标签 `多die_修正比例_修正延迟_修正扩散策略`。
开发分支：`codex/placement-runtime-acceleration`。
正式目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

用户于 2026-09-29 允许调整原算法逻辑。本批以布局合法性、候选图上的匹配数/成本和完整布局后的 QoR 为验收依据，不要求复现旧坐标或旧同分选择。

## 实现

1. `SparseMinCostMatching.h` 按真实候选边找连通分量，局部重编号，并按规模安排 OpenMP 任务。没有按 SLR/CR 删除边或缩小候选搜索范围。无候选边的左节点保持未匹配。
2. 默认 `component_assignment` 使用稀疏矩形指派：从当前新加入的行搜索最短交替路径，维护行/列对偶量，优先选择同距离下的空闲列；只重置访问过的工作数组。它不建立稠密代价矩阵，也不在每次增广时从全体未匹配行的源点重新扫描整张图。
3. 每行加入一个仅供算法内部使用的虚拟列，惩罚为 `(分量行数 + 1) × (最大真实边代价 + 1)`。该值超过分量全部真实边成本上界，确保先最大化真实匹配数，再最小化成本。虚拟列不会成为 FPGA 位置，最终仍返回 `-1`，由原合法化流程扩张候选范围继续求解。
4. 保留 `legacy`、`component_spfa` 和 `component_ssp` 对照。后两者用连续正反向边和严格相反的成本；`component_ssp` 是带势函数的 Dijkstra。原内核的正向 `+0.01`、反向不对称规则不带入新内核。若接口要求的总匹配数小于左节点数，使用全图有总流量上限的内核，避免错误地给各分量分别分配同一个上限。
5. 硬宏候选的 `cached_topk` 对每个合法候选计算一次成本，用 `partial_sort` 选前 K 个，并将选中成本复用到紧接着的建图。缓存每轮重新建立；不跨迭代复用。相同成本以原候选顺序打破平局，会与旧 quicksort 有不同结果。只读 PU 网络列表改为引用。

旧的目标函数、候选数量和搜索半径参数、SLR/级联合法性检查、QP 迭代数、扩散和打包规则在本批没有额外调整。下一批应依据新的实测瓶颈再选择网格维护、扩散或时序维护优化。

## 配置与运行

新增配置均使用 GETRF/U250、8 线程、10 ns、共享及 SA 比例 0.71、物理边界延迟和二维聚拢：

| 配置 | 匹配内核 | 宏候选选择 | 用途 |
| --- | --- | --- | --- |
| `getrf-u250-placement-fast.json` | component_assignment | cached_topk | 常规加速实验 |
| `getrf-u250-placement-fast-verify.json` | component_assignment | cached_topk | 另逐项重新计算所选成本，验证缓存一致性 |
| `getrf-u250-placement-legacy.json` | legacy | legacy | 同构建中的旧匹配/候选模式对照 |

相关键为 `BipartiteMatchingBackend`、`MacroCandidateSelection`、`VerifyMacroCandidateCosts`。旧的已发布标签与冻结可执行文件仍可用于完整历史版本复现。

```bash
cd /Projects/jinyang/workspace/AMFplacer3.0
python3 scripts/amf3.py build --jobs 8 --no-set-current
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-placement-fast.json \
  --binary builds/<本次构建ID>/build/AMFPlacer
```

只测 AMF 时增加 `--amf-only`。按 2026-09-30 用户决定，完整流程保存导入审计后默认允许 Vivado placement 修复并继续 routing；增加 `--strict-import` 可选用严格导入关卡。最终 DCP 保存在运行目录的 `reports/`。

## 当前验证

- 三种新内核各通过 6,104 组穷举最优解对照，覆盖不足匹配、孤立节点、同分、重复边、零成本、浮点成本及低于左节点数的总匹配上限。
- 检查串行/8 线程结果一致性、输出无重复 site、输出边属于候选图；内核拒绝无效/负的候选成本。
- 原项目 70 项 Python 测试通过；内存与未定义行为检查、最终冻结构建和完整 GETRF 回归状态见本次证据目录。回归完成前不宣称最终布局合法或时序通过。

首版分量 SPFA 的 GETRF 试运行 `getrf-u250-full-20260929-010632-375832` 用来采集真实图。其冻结构建为 `build-20260929-010443-527522-33d881d0`。较大连通图仍耗时明显，因此改用稀疏指派；该试运行不作为完成的 placement 样本。

三张原始图的单次回放结果如下。回放时首版 placement 同时在运行，数字用于定位与选择内核，不是隔离负载的最终性能统计。

| 图 | 左节点 / 右节点 | 最大匹配数 | 旧版秒 | 分量 SPFA 秒 | 分量 Dijkstra 秒 | 稀疏指派秒 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 12231 / 10775 | 6244 | 2.026 | 0.250 | 0.609 | 0.00662 |
| 1 | 12231 / 12157 | 8317 | 5.303 | 1.410 | 2.858 | 0.01499 |
| 2 | 12231 / 22948 | 11754 | 47.984 | 41.901 | 67.700 | 0.02946 |

每张图四种内核的匹配数相同。三种新内核的原始成本分别为 `277083.668979645`、`443721.585033417`、`469062.407125473`，各自与其他新内核相同，并略低于旧内核。没有将更低的局部匹配成本等同于更好的 routed WNS。

大型数据与原始图只存服务器：

- `experiments/preflight/20260929-placement-matching/`：编译、穷举、sanitizer 与合成图验证。
- `experiments/evidence/20260929-placement-acceleration/`：真实图哈希、内核回放、运行启动记录和后续完整回归结果。
- `experiments/runs/`：独立 manifest、配置、日志和服务器 DCP。

`checkBipartiteMatching --replay <图文件> <内核> 8` 可回放输入。可选诊断环境变量 `AMF_MATCHER_DUMP_DIR`、`AMF_MATCHER_DUMP_MIN_LEFT`、`AMF_MATCHER_DUMP_MAX_FILES` 采集有限数量的图；正式耗时对比关闭采集与候选成本复核，两组采用同线程数和相同插桩模式，并顺序运行。

## 耗时口径

匹配回放测量包括 matcher 构造与求解。完整运行的 AMF 进程墙钟不能直接称为纯 placement 算法时间。继续分别记录输入读取/建模、AMF 内部计算、诊断/导出、Vivado 适配、`place_design`、`route_design`；未独立计时的部分明确保留为未分段。原 57.17 分钟来自带细粒度插桩的历史运行，不能直接与新普通构建的进程墙钟计算公平加速比。

## 算法背景

矩形指派的最短增广路径与对偶变量可参考 [SciPy 的矩形指派实现及 Crouse 论文出处](https://github.com/scipy/scipy/blob/main/scipy/optimize/rectangular_lsap/rectangular_lsap.cpp)；[SciPy 稀疏匹配文档](https://docs.scipy.org/doc/scipy-1.16.1/reference/generated/scipy.sparse.csgraph.min_weight_full_bipartite_matching.html) 说明了稀疏指派和最小权全匹配的关系。本项目实现针对现有候选边、连通分量和不足匹配情况，不依赖 Python/SciPy 运行时。
