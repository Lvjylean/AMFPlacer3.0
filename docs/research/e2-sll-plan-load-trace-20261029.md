# E2 全量 SLL 计划载入与后端恢复

GETRF/U250/12 ns 的新全量流程已完成综合、AMF 和 Vivado placement，但主会话在载入 63 MB 树形 SLL 计划时开启了 Tcl 回显，长时间无法进入 4 个并行工作进程。已保留原实验记录，从同一轮的 placed DCP 恢复后端。

- 计划包含 24,795 条 net、25,359 个互不重复的物理 SLL 通道。
- 普通 Tcl 加载约 1.8 秒；Vivado source -notrace 加载约 1.0 秒。
- 普通 Tcl 与 Vivado 关闭回显后生成的完整规范化计划内容哈希一致：9ef1411a9f6795878fcf17e4913f63d5939d58e72940b887937c2c6720482a8f。
- 生成数据文件的 source 明确使用 -notrace；全量和恢复入口的 Vivado 命令也关闭回显。工具警告、错误和阶段日志继续保留。
- 后端恢复支持继承 E2 计划及 native cache 提案/接口/上下文，不复用旧静态路由或旧占用缓存；从本轮新路由再次认证、收集占用并封存。
- 原有完整时序、区域、布线、DRC、静态接口认证及 pin-cache 时间上限均保留。
- 恢复 manifest 的 wall_seconds 仅指恢复进程；报告必须另列完整生命周期跨度和中断开销，不能把继承的 AMF 测量误当作本次重新执行。

本轮证据在 experiments/comparisons/getrf-u250-12ns-e2-fresh-full-parallel-20261029-r2/sll-load-profile/。
