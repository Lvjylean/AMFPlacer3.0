# AMF 功能运行时间 profiling

本次使用 GETRF / U250、10 ns、共享与 SA 比例均为 0.71、开启二维聚拢的上一轮配置快照。只执行 AMF 完整布局，不重新调用 Vivado placement/routing；不产生新的 routed DCP。

## 计时方法

`src/lib/utils/RuntimeProfiler.h/.cc` 提供编译期可关闭的 RAII 计时。普通构建默认完全关闭；profiling 构建保留原有 `-O3` 优化，只有设置 `AMF_PROFILE_OUTPUT` 时收集记录。

- 墙钟采用 `CLOCK_MONOTONIC`，每线程 CPU 采用 `CLOCK_THREAD_CPUTIME_ID`。
- 每个记录点包括源码位置、功能分类、调用次数、包含与排他时间、最长单次耗时。
- 主线程排他墙钟求和与 main 计时范围核对，作为不重复累计的功能总表。
- 工作线程墙钟存在重叠，分别保留，不与主线程墙钟相加。工作线程排他 CPU 可相加。
- PaToH 使用独立子进程，分别记录分配、核心划分和其余处理；子进程 CPU 与主进程 CPU 分开。父进程等待中已经包含这些子进程的执行，不二次累计墙钟。
- QP 分开记录目标构建、X/Y 并行求解墙钟，以及线程内的输入检查、稀疏矩阵组装、数值保护、CG 预处理、CG 迭代和解验收。
- 输入解析、Vivado 文件输出、诊断输出、检查和清理单列，不并入布局工作。首次 DCP 导出本轮未执行，不能凭缓存复用声称冷启动成本为零。

此方法测量功能边界，不逐个插桩每个 STL/Eigen 函数、访问器或逐引脚内联操作。未细分调用归入其调用者的排他时间。函数明细的包含时间存在父子重叠，不能逐行累加。OpenMP/库内部未插桩线程的 CPU、计时器本身开销等保留为未归属 CPU，不任意分摊。

## 验证与解释

计时器预检覆盖嵌套去重、多线程、异常展开、显式结束、重复调用以及普通构建关闭行为。采集器核对主线程排他墙钟守恒；列出未执行的编译计时点，不把缺失数据写成已测得的 0。

服务器 `perf stat -e task-clock true` 返回权限拒绝，`perf_event_paranoid=4`。因此本次采用程序内计时，没有修改服务器性能事件权限。

插桩会产生开销，且共享服务器负载、并行调度会影响结果；本报告保留实际测量值，不用统一百分比扣除开销，也不将与历史轮次的耗时差当成算法改进。

## 使用方法

以下命令在 `eda072` 的 `/Projects/jinyang/workspace/AMFplacer3.0` 中执行：

```bash
python3 scripts/amf3.py build --profile --no-set-current --jobs 4
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-sa-calibrated.json \
  --binary builds/<本次构建ID>/build/AMFPlacer \
  --amf-only --profile
python3 scripts/diagnostics/summarize_runtime_profile.py \
  experiments/runs/<本次运行ID>
```

`--no-set-current` 保留原默认构建。每次独立目录保存二进制、配置、输入哈希及源码快照的来源；不要覆盖历史实验。

输出位于运行目录的 `reports/`：

- `profile_summary.md/json`：功能总表、计时核对和解释边界。
- `profile_functions.csv`：全部已测量函数/模板实例的次数和包含/排他时间。
- `profile_instrumentation_coverage.csv`：执行与未执行计时点。
- `profile_categories.csv`：不重复累计的功能汇总。
- `profile_partition_children.csv`：PaToH 子进程明细。
- `amf_profile.tsv` 与 `.meta.json`：线程级原始数据及进程资源使用。

所有源码位置以该轮 `builds/<构建ID>/src/` 的冻结快照为准，避免后续源码行号变化造成误读。
