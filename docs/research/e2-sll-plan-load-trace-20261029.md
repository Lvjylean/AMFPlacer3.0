# E2 全量 SLL 计划载入与后端恢复

GETRF/U250/12 ns 的新全量流程已完成综合、AMF 和 Vivado placement，但主会话在 SLL 任务分片前后耗时异常。最初观察到 63 MB 树形计划被回显，关闭回显后仍存在长延迟；进一步分项测量定位到分片循环，不能把全部耗时归因于回显。已保留中断记录，从同一轮 placed DCP 恢复；恢复轮已进入 4 个工作进程，继续运行，不为性能修复再次中断。

- 计划包含 24,795 条 net、25,359 个互不重复的物理 SLL 通道。
- 普通 Tcl 加载约 1.8 秒；Vivado source -notrace 加载约 1.0 秒。
- 普通 Tcl 与 Vivado 关闭回显后生成的完整规范化计划内容哈希一致：9ef1411a9f6795878fcf17e4913f63d5939d58e72940b887937c2c6720482a8f。
- 生成数据文件的 source 明确使用 -notrace；全量和恢复入口的 Vivado 命令也关闭回显。工具警告、错误和阶段日志继续保留。
- 后端恢复支持继承 E2 计划及 native cache 提案/接口/上下文，不复用旧静态路由或旧占用缓存；从本轮新路由再次认证、收集占用并封存。
- 原有完整时序、区域、布线、DRC、静态接口认证及 pin-cache 时间上限均保留。
- 恢复 manifest 的 wall_seconds 仅指恢复进程；报告必须另列完整生命周期跨度和中断开销，不能把继承的 AMF 测量误当作本次重新执行。

本轮证据在 experiments/comparisons/getrf-u250-12ns-e2-fresh-full-parallel-20261029-r2/sll-load-profile/。
## 分片循环实测与修正

- 同一份 24,795 net 计划：Vivado 无设计会话 source 1.038 秒、权重生成 0.122 秒，旧分片循环在 60 秒边界仍未结束。
- 旧循环每次将大型 net 记录 concat 回嵌套列表，再 lset 回主列表；将每个桶改为独立 Tcl 数组元素，用 lappend 累加，最后合并。
- 新分片在 Vivado 中耗时 0.805 秒，4 桶权重保持 134944 / 134944 / 134952 / 134952。
- 新 Vivado 分片与旧原生 Tcl 分片的完整规范化内容 SHA-256 均为 faaa9c431fb87fb4e86da9beffdbf7eb8aa42c31f6ca16c7caaa1e1693b37d09：桶归属、桶内次序及树内容逐字一致。
- 这证明是循环实现层面的性能差异，不改变 SLL 规划、物理通道、锁定规则或线程数；尚不把该独立测量当成整轮耗时。

## CARRY8 源出口恢复

- 全量约束准备随后暴露两处问题：COUT 无下游节点时空查询报错；只重置 FIXED_ROUTE 属性未清除导入时的专用 COUT 路由状态，局部路由因源端无法进入普通布线网络失败。
- 修复在 native 路由前增加对该 net 的 unroute；结束后仍撤销试探线路，只保留 Vivado 选择的物理出口和原计划 SLL 树。AMF 不指定 HMUX 等具体出口。
- 同一新 placed DCP 上五条 CO7 实测全部通过，其中含三条原失败连接、两条成功参照；Vivado 均自行选出可达普通网络的出口，重新导入原 SLL 约束通过。该验证不等同于整设计布线完成。
- 边界空下游、carry-only、可达 fabric、非 carry、唯一出口及异常清理的单元验证通过。
- 初期诊断脚本存在文本替换及样本匹配错误，相关尝试均标为无效证据并留档；正式依据为 carry-source-recovery-v4 与 carry-source-five-net-validation.json。
