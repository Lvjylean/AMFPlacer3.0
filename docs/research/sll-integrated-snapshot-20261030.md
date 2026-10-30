# sll-增量合并与加速：当前功能归档

本分支整合增量编译、E2 SLL 优化/约束导出加速、严格审计加速及本地布局查看器。**SLL 基础线长建模有已确认问题，本次不修改 L**，见 [已知问题及待实施公式](sll-modeling-known-issue-20261030.md)。

## 代码组成

| 内容 | 来源与入口 | 范围 |
| --- | --- | --- |
| E2 与增量编译 | 继承 fbc8459 及其历史；`scripts/run_dfx_full.py`、`scripts/run_parallel_incremental.py` | 全量静态缓存、双模块并行增量、联合路由、静态/接口与 PR 检查；DFX 接口沿用原生路由兼容策略 |
| SLL 导出加速 | 5fba396；`scripts/sll_routes.tcl`、`sll_parallel.*` | 批量解析源端，分片生成/导入；小计划走会话内路径 |
| SLL 严格审计加速 | 2efea471；`scripts/sll_audit_parallel.tcl`、`sll_audit_collect.py` | 有界并行与精确覆盖合并；保留原审计范围 |
| 本地可视化导入 | `scripts/diagnostics/placement_playback/` | SLL 入口/出口 tile、容量、Pblock、全量与独立增量布局；保留现有 Mac picker 源码 |
| 已验证脚本快照生成 | `scripts/run_dfx_full.py` | 使用成功实验的原样脚本，保留 support 文件打包修复；放置后报告单独计时 |
| GETRF CARRY 兼容覆盖包 | `variants/getrf-u250-12ns-20261030/` | 特定 CARRY net 的可选预连接；不作为默认后端 |
| 高扇出拓扑保护历史记录 | 上述目录的 `historical-topology-guard/` | 仅用于追溯，不由当前默认入口或可选 CARRY 覆盖包调用 |

按用户最新约定，正式入口使用默认 `place_design`，不启用阻止高扇出驱动复制的 DONT_TOUCH 拓扑保护。该约定不是 Vivado 行为的普遍保证；实际连接若变化，现有 SLL 一致性检查仍会拒绝不一致计划，不自动修改或放宽验收。GETRF CARRY 覆盖包含精确 net 名，不能作为任意设计的默认补丁。历史完整实验曾使用拓扑保护；本次取消其调用后的组合未重新进行完整布局布线，不沿用旧实验宣称新组合已完整通过。

## 可视化使用

从已有 AMF 报告生成 bundle，再复制 bundle 到本机；不需要下载 DCP：

```sh
python3 scripts/diagnostics/placement_playback/build_layout_bundle.py \
  --run /absolute/path/to/run --full /absolute/path/to/full-run \
  --placement /absolute/path/to/placement.tsv \
  --out /absolute/path/to/new-bundle --title 'GETRF 布局'
python3 scripts/diagnostics/placement_playback/local_layout.py \
  --ensure --bundle /absolute/path/to/new-bundle
```

生成器与本地分层查看器使用 Python 标准库；旧紧凑预览/GIF 工具有 Pillow 依赖。Mac picker/页面租约依赖随源码一并保留在原相对路径；用户的运行目录、已注册数据、端口状态与缓存不入 Git，现有本地安装不覆盖。独立 `local_layout.py` 入口不依赖 picker 的运行记录。

若实验仅有最终快照，查看器只显示一帧；不会生成不存在的 QP 历史。SLL 图层表示规划值，不能冒充实际 routed SLL。增量静态上下文仅含输入缓存中提供的相关 cell，不是完整静态 shell。

## 验证依据与边界

- SLL 导出：原验证包含 65-net 小样例等价性及 24,624 内部 net 的完整生成/导入；记录见 `docs/research/sll-export-speed-20261030.md`。
- 严格审计：既有完整 DCP 验证保留 333,925 接收端审计行，串行/并行规范化输出一致；两 worker 审计 1,634.216 s，对照原审计 4,986.146 s。它们不是受控独占资源实验，也不是编译总墙钟。
- GETRF/U250 12 ns 无 SLL 位置约束轮：全量 V0 与并行增量 V1 均完成，WNS +0.030 ns、WHS +0.010 ns；完整 PR 检查通过。它保留虚拟 SLL/容量/时序功能，仅关闭位置约束，不能证明固定 SLL 的 QoR 已修复。
- 本地查看器既有验证覆盖 855,092-cell 全量快照、25,359 个 crossing、两个独立增量模块、Pblock 与精确通道搜索；原验证为 25 项测试和真实 UI 检查。归档时另做源码回归验证，记录见 `docs/provenance/sll-integrated-snapshot-20261030.json`。
- 所有既有结果来自各自冻结源码及独立部署。本次不启动新的完整布局/布线，不重新计量加速比例，不修改实验数据或默认参数。

实验索引为正式项目下 `experiments/comparisons/getrf-u250-12ns-integrated-speed-20261030/`（其实际目录位于导出加速 preflight 副本下）及 `experiments/comparisons/getrf-u250-12ns-free-sll-20261030/`。最终 DCP 继续只保存在服务器。
