# R10 完整实验已完成

完成时间：2026-09-29 15:04:20 UTC+8。正式 WNS +0.036 ns，TNS 0，setup/hold 违例端点 0；SLL 10,070；AMF 加权 HPWL 3,190,300.817061。严格导入、全量布通、DRC 与级联验收通过。

AMF 实际布局 35.49 分钟，进程 37.55 分钟；Vivado placement 2.88 分钟，routing 26.42 分钟；总墙钟 90.59 分钟。详细 R09/R10 表格、配置、DCP 地址、profiling 与计时口径见 [getrf-r09-r10-results.md](getrf-r09-r10-results.md)。

最终 DCP：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380/reports/getrf_routed.dcp

DCP SHA-256：fa1973d8766aaebfd12d3381d78cacd2aaee94bf46cace793c1fbe54c8698d49

2026-09-29 后续评测决议：用户查询确认，R10 最差路径首段的绕行来自高扇出负载分布过于零散，driver 放置与共享布线树需要兼顾众多负载；不将其归因于布线资源紧缺。高扇出优化留待后续，GETRF 暂不作为当前优化工作的主要效果评测 case。已有实验结果保留。归因更正和原始证据见 [关键支路诊断](getrf-r10-critical-route.md)。

以下保留启动与阶段进展记录：

---

# R10：新坐标、延迟模型、原版比例与扩散修复

状态：已启动完整实验，包含 profiling。

- 运行目录：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380
- Profiling 构建：/Projects/jinyang/workspace/AMFplacer3.0/builds/build-20260929-132823-663969-c70df682
- AMF 二进制 SHA-256：991a23d9a06b3a68f43df1bee990459f2a9703c74702cf5e3b290974e54bf8c9
- 完整流程 PID：1987485
- 自动收尾 PID：1987506

配置：GETRF/U250，10 ns，共享 y2xRatio=0.4，SA 按原规则为 0.32；新 tile 列坐标与对应中/远段延迟系数；二维聚拢开启；legacy 二分图匹配；cached_topk；包含 disY 修复。Profiling 构建与已验收非 profiling 构建的全部源码哈希一致。

计时：AMF 核心布局使用主线程排他功能范围求和，输入、输出、验证、初始化与清理单列；AMF 进程墙钟保留用于核对。AMF→Vivado 导出、Python 转换、Vivado 导入、place_design、route_design、检查/报告、DCP 写出分别记录。复用已有 DCP→AMF 缓存，本轮未重新执行该导出。

验收：AMF 最终加权 HPWL；Vivado routed WNS/TNS；原生 utilization.rpt 中 Total SLLs Used、每道边界及方向的 SLL 使用量；严格导入、全量布线、DRC 与级联合法性。HPWL 受坐标及权重变化影响，不直接计算相对 R09 的改善百分比。SLL 不等于唯一跨 SLR net 数。

自动收尾输出 reports/profile_summary.json、profile_summary.md、profile_functions.csv、profile_categories.csv、r10_metrics.json、r10_summary.md；每 15 秒记录服务器负载与相关进程 CPU/RSS。报告插桩开销不伪造扣除。

最终 DCP 预期地址（尚未生成）：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380/reports/getrf_routed.dcp

## AMF 阶段完成，等待后端验收

核对时间：2026-09-29T14:16:40.952846+08:00。AMF 实际布局功能墙钟 2129.507 秒（35.49 分钟）；AMF 进程墙钟 2253.141 秒（37.55 分钟）。Profiling 记录完整，主线程排他耗时求和误差小于 1e-9 秒。最终 AMF 加权 HPWL=3190300.817061，使用本轮新坐标与 y2xRatio=0.4，不能直接与 R09 原始值计算改善率。

Vivado 已取得 Implementation 许可证，正在导入 AMF 布局。严格导入尚未完成，最终 WNS/SLL 与全量路由验收仍待后端结束。当前进度证据：reports/r10_progress.json；功能明细：reports/profile_summary.md、profile_functions.csv。
