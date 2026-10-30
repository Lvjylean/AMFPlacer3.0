# GETRF/U250 12 ns 可选 CARRY 后端覆盖包

来源是 `experiments/deployments/getrf-u250-12ns-free-sll-20261030/scripts/` 中相对 2efea471 的运行补丁。根据用户最新约定，当前默认后端和本覆盖包均不调用高扇出拓扑保护。该保护的 helper、名单和原始完整 backend 放在 `historical-topology-guard/`，只作历史诊断记录。

当前 `scripts/` 仅含三个文件。需要已确认的 CARRY workaround 时，在新的独立部署中将其覆盖到根 `scripts/`；通用默认后端不使用该覆盖包。CARRY 预连接只在 `e2_sll_enabled` 为真时执行，关闭位置约束时跳过。缺失指定 net 时仍拒绝执行。不要覆盖历史实验输入或用于不匹配的设计。

当前三个文件：

- `dfx_full_backend.tcl`：默认 place_design 后，按原 SLL 计划预连接一条 CARRY 输出，再完整全局 route；不调用拓扑保护。
- `dfx_full_carry_preroute.tcl`：核验原计划、CARRY8 源、连接与位置/pin 锁定不变、ROUTED 和计划 SLL。
- `run_dfx_full.py`：已验证的完整输入快照打包代码，与本分支根 scripts 中版本相同。声明的 support 名单若存在也会被快照保存，不因后缀为 txt 而漏带；这不等于启用拓扑保护。

`tests/` 保存已有小型守卫测试；它们不能替代真实 Vivado 布线和 PR 验收。源 helper 有既有实际实验验证；本次去掉拓扑保护调用后的组合未完整重跑。真实实验及失败恢复来源见分支归档说明与 provenance。SLL 的 L 表达没有修改。
