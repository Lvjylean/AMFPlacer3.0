# SLL 分支的连通子图隔离与线程一致性（2026-10-17）

在已发布 `sll优化` 提交 `0461efeb59fc0e1af6a6e0175e8a177b64b3b36e` 上，移植历史 `4dcfefded476cb9571e43e3edb4ff0d7830b8ed2` 的 `legacy_components`。未移植 grid/坐标更新加速。保留独立 SLR 每次 cluster 最多两轮、普通 Pro、`ProPrimitiveTiming=false` 和原距离系数。

## 修改内容

旧 `legacy` 将连通子图按 `component_id % jobs` 合并到多个求解器，同一求解器中的子图共享超级源、汇和增广过程。改变线程数量会改变分组。

`legacy_components` 先根据候选二分图识别连通分量，按全局编号排序各分量的左右节点，保留每行候选边的原有顺序。每个分量独立建立 `MinCostFlow`，具有独立的源、汇、残量图、队列和距离数组。OpenMP 动态调度分量任务，`jobs` 只决定工作线程上限。各任务写互不重叠的左节点结果，统计和哈希在任务结束后按固定分量顺序汇总。

原生 `MinCostFlow.h` 未修改，保留 float 成本、正向边内置 `+0.01` 和既有容差。线程独立性不意味着与旧分组合并方式输出完全相同，也不承诺原始边成本的严格全局最优。该模式接受全左节点匹配请求或零请求；部分全局配额显式拒绝，避免错误分配配额。现有生产合法化调用均请求全左节点匹配。

SLL 原先为每个工作线程维护累加量，线程数变化会改变浮点加法分组。现在采用最多 16 个固定逻辑分块，net 在块内按编号计算，块间按固定顺序汇总。1/12/16 个工作线程执行相同的运算分组；不使用浮点原子加法。SLL 模型、解析导数和 QP 公式不变；SLL 这一部分最多并行执行 16 个块。其他阶段仍使用自己的并行策略。

## 使用

推荐配置：`configs/experiments/getrf-sll-legacy-components.json`。

```json
{
  "BipartiteMatchingBackend": "legacy_components",
  "jobs": "12"
}
```

切换时只将 `jobs` 改为 `"16"`，无需重新编译。历史 `getrf-amf3pro-srl-share4-10ns.json` 和省略匹配后端时的 `legacy` 默认值均保留；要获得子图隔离，应显式选择新配置或设置上述后端。

配置中的 `jobs` 是布局运行线程数；`amf3.py build --jobs` 仅控制编译并行度，两者分开设置。

新配置默认数量目标和诊断关闭、`SllRegionPolicy=preserve`。如需开启数量目标，设置 `SllQuantityEnable=true`，并给 `SllReportDirectory` 指定本次实验独立的报告目录。权重 100、tau 30、gamma 0.25 等保留为已有实验参数，不代表重新标定。

构建与源码核验：

```bash
python3 scripts/verify_sll_release.py
python3 scripts/amf3.py build --jobs 8 --profile --no-set-current
```

在配置输入路径和服务器 machine 文件准备好后，用同一冻结二进制复现一对完整 AMF 布局：

```bash
python3 scripts/diagnostics/verify_matching_threads.py \
  --repo "$PWD" \
  --binary /absolute/path/to/frozen/build/AMFPlacer \
  --config configs/experiments/getrf-sll-legacy-components.json \
  --dcp /absolute/path/to/post_opt.dcp \
  --output /absolute/path/to/new-comparison-directory \
  --quantity on
```

`--quantity off` 验证关闭组。脚本顺序运行 12、16 线程，使用 `amf3.py full-run --amf-only`；同组仅 `jobs` 和报告输出目录不同。两次运行必须成功，且输入哈希、每次匹配的图/结果/搜索计数、最终布局 Tcl、SLL 阶段记录、QP 记录及映射记录均相同，才返回成功。输出目录必须全新，失败证据保留。脚本不执行 Vivado。

## 本轮验证

四次 GETRF / U250 / 10 ns 完整 AMF 布局均成功；每组内使用同一二进制，按 12→16 线程顺序执行。完整来源、输入哈希、配置、构建信息和文件哈希见 [验证记录](../provenance/sll-legacy-components-validation-20261017.json)。

| SLL 数量目标 | 主机 | 12/16 线程匹配调用 | 12/16 线程 SLL QP 记录 | 最终布局 Tcl | SLL 阶段/映射记录 |
| --- | --- | --- | --- | --- | --- |
| 关闭 | ee4e072 | 185 / 185，全部一致 | 0 / 0 | 逐字节一致 | 280 / 9 条，逐字节一致 |
| 开启，权重 100、preserve | coldsun / eda099 | 145 / 145，全部一致 | 80 / 80，全部一致 | 逐字节一致 | 280 / 9 条，逐字节一致 |

每次匹配均比较图哈希、匹配哈希、成本、匹配数量及各分量搜索计数汇总。两组匹配调用数不同来自优化目标不同；线程一致性只在相同目标的 12/16 线程之间比较。

| 组别 | 12 线程 AMF 进程墙钟（分钟） | 16 线程 AMF 进程墙钟（分钟） | 两次最终加权 HPWL |
| --- | ---: | ---: | ---: |
| 数量关闭 | 18.8274 | 18.5747 | 4792511.340916 |
| 数量开启 | 19.6664 | 19.3371 | 4164979.834915 |

这些时间来自各一次实际运行，用于追溯，不作为统计意义上的加速结论。两个目标组运行于不同主机，不能据此计算目标开关的耗时收益。

最终 Tcl SHA-256：

- 数量关闭，12/16 相同：`04f6671c126ec4145f7f7c2248a5b86415d9cf7deae17d2500bacae04cc38470`。
- 数量开启，12/16 相同：`3cb8923829a4f0405f53df1e674eb077c15d4cc00beda614eca9758f5f2b718f`。

实验保留在两个服务器各自项目根目录下的 `experiments/preflight/20261017-sll-legacy-components/`。构建来自 `3f7b1354`；随后修正的空 net 测试用例及运行脚本位于 `aa70eb07`。逐文件核验确认全部生产源码与冻结构建一致，修正后的 94 项 SLL 测试在两台服务器独立编译通过。

基础检查已通过：718 个匹配图（最大匹配数独立验证、单连通图原生结果对照、互不相关分量隔离、1/2/8/12/16 线程结果及搜索计数一致）；94 项 SLL 检查（有限差分最大绝对误差约 `1.15e-11`，跨线程目标和梯度精确相等）；QP 稳定性、边界策略及 169 项 Python 回归测试。

验证只针对相同输入、相同编译环境和相同二进制下的线程切换。搜索计数哈希不是逐条访问轨迹；未执行 Vivado，因此本轮没有新的 routed WNS/TNS、DRC 或 DCP 结论。不同服务器的运行耗时不用于加速比计算。历史日志和来源清单保持原样，通过新增来源清单叠加核验当前源码。
