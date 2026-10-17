# DFX 独立划分与首次全量边界留空

本实现位于独立 48 副本，基于发布提交 dd6a643d582275a357b2c1c4e90de4335f2ecd3c。
普通 AMF 配置默认关闭；已发布副本和已完成实验不改写。

## 约定

- full-run job 显式增加 "dfx_partition_floorplan": true，保留用户给定的 partitions/Pblock。
- A/B 是各自独立的粗粒度 cluster。不是刚性物理宏，不将内部 cell 设为 fixed。
- 在时钟/长路径聚类前按完整层级前缀认领各 RP 的全部 PU；仅静态 cluster-unit 子集进入递归划分，保留原 net/node ID。
- 初始 floorplan 采用用户确认的 Pblock 几何中心。中心由器件模型中的完整 clock-region 边界得到，不因边缘留空而移动。
- A/B 与静态 cluster 的连接按原邻接权重转换为精确坐标端点；抵消普通 fixed-terminal 额外倍率，避免无意增强牵引。RP 之间代价为常数，不参与静态 SA 搜索。
- 静态 SA 使用区域外合法粗粒度格点；后续所有非 fixed cell 继续在原全局优化流程中求解，并保留硬区域、精确 site mask、CARRY 分区域合法化。
- 初始 floorplan 的几何距离代价仍是启发式，不代表已预测 DSP 引脚可布性或布线拥塞。

## 首次全量边界规则

首次全量 prepare 在现有 Vivado 进程导出真实 site 类型、clock region、PROHIBIT 和 Pblock 几何。
AMF 内部复用 BoundarySitePolicy，在建立候选 site/bin 之前计算留空：

- 完整矩形 clock-region Pblock：SLICE/DSP 最外上下 site 行不放置；内部 CR 接缝不额外留空。
- 左右列只因侧边内部实际 PROHIBIT 扩展；角点不触发整列留空。
- BRAM 无附加边界留空，真实 PROHIBIT 仍生效。
- 原始 ownership mask 与有效 placement mask 分开保存。禁止把移除的 RP 边缘站点归还静态区域。
- 不更改 Pblock 或 routing footprint，也不缓存旧动态模块 routes 来预测新模块。
- 后端覆盖校验读取有效 mask；静态缓存保留原始区域，再用同一策略在全量验收后认证供增量使用。

新模式拒绝 random/external initializer、旧格式精确 prepare 恢复和旧 prepared-input 复用，以免跳过新必需的区域/资源证据。
普通模式和已有恢复入口保持原行为。新模式不增加资源充足性求解器、不自动扩大 Pblock。

## 输出

- dfx_partition_floorplan.tsv：每个 cluster 的区域、层级、PU 数、物理代表位置、对外连接权重；每个 RP 必须且只能有一行。
- hard_region_sites.tsv：原始 ownership，不修改。
- hard_region_sites.effective.tsv：首次全量实际 placement mask。
- full_edge_policy_audit.tsv：SLICE/DSP 边界、PROHIBIT 触发及留空计数。
- manifest 记录上述文件与输入/构建的哈希。

## 验证范围

单元/集成测试目标 checkDFXPartitionFloorplan 覆盖生产子集划分、层级前缀、首次全量 mask、精确端点代价、排除 RP 格点、单格终止和默认 SA 目标。
既有 checkBoundarySitePolicy、checkNativeStaticCache、checkNativePlacementRepair 继续回归。
scripts/diagnostics/check_dfx_floorplan_initial.py 仅回放真实完整输入的 partition/初始 floorplan，记录输入不变性，并逐 cell 核查 cluster 区域归属。
初始化通过不代表完成全局布局、合法化、联合布线或 bitstream 验收；后续新全流程需独立实验与实际验收，不借用旧结果。

## 本次验证结果（2026-10-17）

最终干净构建：build-20261017-155530-255946-dd6a643d。
AMF 二进制 SHA256：d2c4183f1f47027caa6b31d34a721b04a96fde7f4782bacffac993689cb0f573。
构建基于上述发布提交加冻结补丁；源码逐文件哈希与本副本 src 一致。不是声称发布提交本身包含这些改动。

- 新 DFX 集成测试通过；原边界策略 25 项、静态缓存 45 项、单 DSP 修复 14 项通过。
- 分区域 CARRY DP 的 500 组穷举、完整宏上取整/接收预算等既有用例通过。
- 普通模式真实小型输入：发布版与新版的初始化报告、全部 cluster 归属、全部 cluster 初始格点坐标逐字节一致。
- 真实 GETRF 12 ns / 右三个 clock region 输入初始化：855,088 个原始 cell 全覆盖，
  静态 849,188、A/B 各 2,950；580,448 个 PU 分为 62 个静态 cluster 和各 1 个 A/B cluster。
- A/B 各 2,038 个 PU；中心分别 (47.875,149.5)、(47.875,329.5)，单位为 AMF 器件坐标。
  A/B 导出的各 3,693 个 cluster 条目包含 743 个打包生成的虚拟 cell，不能当作 3,693 个原始 cell。
- 初始输出 fixed_pus=0、locked_pus=0：固定的是 RP 的粗粒度代表位置，不是其内部 cell。
- 有效站点集合 237,240 项，与历史已批准规则结果逐项相等；合计移除 392 项，
  即每个 RM 的 172 个 SLICE + 24 个 DSP；左右额外留空为 0。
- 成功初始化进程 104.501 秒，包含器件/网表读取、打包、时序图、partition、SA 和诊断导出。
  这是初始化验证耗时，不能作为全量编译时间或加速比。
- 既有输入哈希全部不变。未运行新 Vivado 布局、布线或 bitstream 验收。

开发过程保留：build-01/build-02 为早期快照，最终构建是 build-03；
首次普通/GETRF 初始化测试因诊断 Dump 路径重复前缀分别退出 -6，耗时 7.510/101.011 秒。
仅修正诊断脚本后使用同一最终二进制重测，失败现场保留。未把测试导出问题归因为放置/布线失败。

完整轻量证据位于本副本上级 evidence-072/ 和 evidence-099/。大输入及运行产物留服务器。
