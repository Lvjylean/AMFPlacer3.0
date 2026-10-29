# SLL 约束导出加速（2026-10-30）

本次只优化后端约束生成与导入，不改变 AMF 布局、SLL 规划、容量目标或时序模型，不需要重新编译 C++。原分支和历史实验未修改。

## 代码与行为

- 新分支：`codex/sll-export-speed-20261030`。
- 服务器副本：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261030-sll-export-speed/repo`。
- 来源：最新 DFX 原生接口回退版本 `fbc8459f3974250b685a8ed4f635572025e5d47a`。
- 修改：`scripts/sll_routes.tcl`、`scripts/sll_parallel.tcl`。
- 运行证据：该副本上一级目录中的 `validation-jobs.json`、`baseline-sample/`、`optimized-sample/`、`full-export/`、`small-route/`。

1. **批量解析特殊 CARRY8 出口。** 原实现为每条特殊 net 分别调用“拆线、原生路由、再拆线”。新实现每个 worker 合并为一组调用，仍由 Vivado 选择出口；分别提取每条 net 的出口，清除探测布线，再恢复各自原来的 SLL 树。失败也会尝试清理整批探测路由。
2. **避免重复构造远端树。** 已校验的 `tail` 已经省略本地接收端 waypoint，直接复用，不再次遍历全部远端端点。多源侧 crossing 的预览树和实际连接检查仍保留。
3. **批量锁定端点。** LOC/BEL 固定标志、REF_NAME 查询使用集合操作；已有位置索引用于导出。LUT 的引脚映射锁定仍逐 cell 保留。
4. **小计划在当前 Vivado 会话执行。** 同时满足 net 数不超过64、树工作量不超过4096时，避免写出完整 DCP 和启动额外 worker。工作量为每个接收端1、每个 crossing 额外8；这是调度阈值，不是 SLL 优化公式。仍保存兼容后续审计的 shard、records、locks 和 XDC。大计划仍按原配置并行。

无新增用户功能开关；仍由 `SllRouteConstraintEnable` 控制生成与导入。DFX 接口继续按现有策略交给原生布线，未重新启用问题接口的精确约束。

## 同输入速度对照

同一台 eda072、Vivado 2026.1、GETRF/U250、12 ns、同一份布局 DCP 和65条 net，串行运行原版与新版。样本包含3条需要特殊出口解析的 CARRY8 net，专门覆盖慢路径，不能直接外推全部设计的加速比例。

| 阶段 | 原版 | 新版 |
|---|---:|---:|
| 打开输入 DCP | 132.073 s | 132.342 s |
| 生成并应用 SLL 约束 | 443.922 s | 231.376 s |
| 其中：特殊出口解析 | 422.179 s，3次 | 209.202 s，1批 |

约束生成/应用减少 **47.88%**，约 **1.92×**。表中不是包含 Vivado 启动、退出的进程总墙钟。

正确性对比：65条 net 记录、793个端点 cell 的锁定属性、65棵导入后的 FIXED_ROUTE 全部一致。两份导出 XDC 的 SHA-256 也完全一致：

`d720e3164326028b41e0743824142a74675d3c5ddf0540bee0190647fc2e4ffc`

## 完整 GETRF 约束验证

复用已有完整布局，未重新执行 AMF 或 GETRF 全量布线。24,795条计划经现有 DFX 策略分类后，24,624条保留精确约束，171条接口使用原生布线。

| 阶段 | 新版实测 |
|---|---:|
| DFX 计划分类 | 30.963 s |
| 写出 worker 输入 DCP | 81.860 s |
| 并行 worker 阶段 | 705.443 s |
| 合并、锁定和主会话导入 | 156.404 s |
| `apply_parallel` 总计 | 945.073 s |
| 外层调用计时 | 945.319 s（15.755 min） |

分类另计；初始 DCP 打开和验证报告也不包括在945.319 s内。各 worker 内的纯生成阶段为539.995–556.688 s，不能与上述总墙钟相加。

完整检查通过：24,624条 net、320,957个逻辑接收端、25,169个计划 crossing、122,863个端点 cell。与旧记录相比，分组、通道、出口选择、导出 XDC 树的差异均为0；所有目标约束已导入，LOC/BEL、LUT LOCK_PINS 和12 ns时钟保持不变。

旧轮次 `getrf-u250-12ns-e2-fresh-v0-20261029-r2-backend-multiroot` 的 `full_sll_import` 为1,934.806 s，worker 阶段为1,625.107 s。该历史轮次包含后来回退的171条接口，且并非本轮重跑的相同负载对照，因此只作耗时参考，不将两者之比宣称为严格加速比。

## 验证边界

批量处理的单元测试覆盖：每 net 树保留、一次批量路由、失败清理、重复请求拒绝、小计划直接执行和审计记录交接。既有 CARRY、多出口、SLL 树结构、资源预留和 DFX 原生接口回归通过。

两个真实 Vivado 小样例 `lut_lower_local_upper`、`lut_shared_control_access` 均通过：多物理出口、上下两侧 crossing、共享接收端节点、独立完整布线、逐接收端路径审计、并行审计、DCP 回读，以及故意错误分组的负对照。二者均走小计划当前会话生成路径，没有额外的生成 worker。它们是约束正确性测试，不是时序达标测试。

GETRF 本轮只验证约束生成与完整导入，不据此宣称布线后 WNS 或可布线性改善。

验证工具整理至 `scripts/diagnostics/benchmark_sll_export.tcl`、`compare_sll_exports.tcl`、`validate_sll_full_export.tcl`。本轮执行时的原样脚本保存在实验目录 `validation-tools/`，路径整理对应关系见 `diagnostic-relocation.json`；历史运行命令和日志未改写。

新增阶段明细：worker 的 `sll-generation-timings.tsv`，以及大计划主会话的 `sll-export-timings.tsv`。中间 DCP 仅保存在服务器。
