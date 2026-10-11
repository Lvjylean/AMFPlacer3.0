# GETRF：SRL共享site四组实验最终结果

2026-10-12 01:34 HKT核验：四组完整AMF→Vivado均已完成并通过实现验收。最后一组完成于01:31:50。两台有限生命周期supervisor均completed，原始来源文件保护校验均为true；未恢复定时检查。

条件：GETRF/U250、10ns、普通Pro、关闭延迟特例、无外部floorplan、AMF 8线程、Vivado2026.1 4线程、repair导入。保留独立PU，每个SLICEM最多4个兼容SRL primitive；撤回按需重算，恢复原MCLB/CARRY耦合调用。

## 性能与时间

时间单位为分钟。

| 指标 | 独立两轮/弱关 | 独立两轮/弱开 | 合并/弱关 | 合并/弱开 |
|---|---:|---:|---:|---:|
| 服务器 | eda099 | eda099 | eda072 | eda072 |
| 正式WNS/ns | +0.035 | **+0.160** | +0.016 | +0.029 |
| TNS/ns、setup违例 | 0、0 | 0、0 | 0、0 | 0、0 |
| WHS/ns | +0.019 | +0.019 | +0.019 | +0.021 |
| hold违例 | 0 | 0 | 0 | 0 |
| AMF进程 | 21.87 | **21.71** | 26.78 | 27.12 |
| GP函数累计 | 13.22 | 13.35 | 17.07 | 17.35 |
| GP内宏合法化累计 | 5.17 | 4.94 | 7.49 | 7.62 |
| 其中MCLB合法化 | 0.60 | 0.63 | 1.10 | 1.11 |
| MCLB调用次数 | 29 | 29 | 31 | 31 |
| 最终packCLBs函数 | 6.97 | 6.66 | 7.63 | 7.75 |
| Python格式适配 | 0.73 | 0.72 | 0.85 | 0.86 |
| Vivado导入 | 10.77 | 10.76 | 15.15 | 14.13 |
| Vivado placement | 4.38 | 4.41 | 5.31 | 5.39 |
| Vivado routing | 22.41 | **22.29** | 41.25 | 29.75 |
| Vivado进程 | 46.69 | 46.59 | 72.51 | 60.99 |
| 完整总墙钟 | 69.85 | 69.60 | 100.76 | 89.68 |
| 全局布线迭代总数（含首次） | 2 | **1** | 4 | 3 |
| Phase5额外hold迭代 | 0 | 1 | 0 | 0 |
| 初期Global/Short最大拥挤尺寸 | 8×8 | 8×8 | 64×64 | 16×16 |
| 初期timing congestion | 5（32×32） | 5（32×32） | 7（128×128） | 7（128×128） |
| SLL实际用量 | 10332 | 11656 | 10110 | 10323 |
| AMF加权HPWL | 3997071.330305 | 4143975.932820 | 5198651.053249 | 5140218.651898 |

WNS取最终timing_summary.rpt，router日志摘要分别为0.034/0.139/0.015/0.028ns，不混用。所有route_design均调用一次；表中全局迭代是其内部Global Iteration 0起的轮数，含首次；额外hold单列，正常Post Hold Fix阶段未混入此项。

GP采用GlobalPlacement_CLBElements主线程函数的累计inclusive_wall_s，含函数内阶段切换；与旧文档phase completed日志求和存在小幅口径差异。宏合法化包含MCLB，GP包含宏合法化，AMF进程包含GP与最终打包，不能把这些行相加。packCLBs包含内部补充合法化和详细布局，不能当作单纯打包或单独补充合法化时间。Vivado进程含导入、布局、布线、审计、报告和DCP写出；总墙钟取manifest.started到status.finished。跨主机墙钟不宣称为纯算法加速比。

## SRL资源与正确性

从真实AMF最终导出按SRL metadata逐实例匹配统计，并由routed LOC/BEL保留审计验证位置未变：

| 组 | SRL primitive数 | 使用SLICEM site | 每site含1/2/3/4个SRL的site数 |
|---|---:|---:|---|
| 独立两轮/弱关 | 12431 | 3171 | 20 / 60 / 73 / 3018 |
| 独立两轮/弱开 | 12431 | 3179 | 33 / 54 / 78 / 3014 |
| 合并/弱关 | 12431 | 3176 | 28 / 56 / 77 / 3015 |
| 合并/弱开 | 12431 | 3172 | 22 / 52 / 87 / 3011 |

无SRL缺失、无site超过4个SRL；相对原12231个排他宏各占site的分配，减少约74%的SRL site需求。此表不表示整设计全部SLICEM占用，也不表示密度权重按同样比例减少。

四组均routing_complete=true，DRC Error/Critical Warning为0；856998个AMF请求实例全部导出，布线后原始LOC/BEL全部保留；12183条专用级联、325条SRL级联均零违例。策略仍标repair，不改称strict导入。

## 与共享前对应历史对照

历史弱关为10月11日16:13的pro-stage-legalization-study；弱开为20:28的mclb-recompute-efficiency/coupled组，均为原耦合重算，不使用own-distance组。

| 组 | AMF进程：旧→新/min | WNS：旧→新/ns | route：旧→新/min |
|---|---:|---:|---:|
| 独立两轮/弱关 | 30.01 → 21.87 | +0.133 → +0.035 | 19.69 → 22.41 |
| 独立两轮/弱开 | 44.27 → 21.71 | +0.138 → +0.160 | 23.46 → 22.29 |
| 合并/弱关 | 42.57 → 26.78 | +0.120 → +0.016 | 20.70 → 41.25 |
| 合并/弱开 | 61.87 → 27.12 | +0.002 → +0.029 | 36.47 → 29.75 |

共享的效率改善在四组中一致，MCLB仍调用29/31次而累计仅36～67秒，不能解释为跳过合法化带来的省时。时序收益不一致，不可宣称SRL共享必然改善QoR。本轮独立两轮/弱开最符合用户的软目标：WNS>0.1ns、全局布线1轮、timing congestion=5、GP宏合法化约4.94min、AMF进程约21.71min。合并两组虽过时序，但3～4轮全局布线、timing congestion=7，不满足期望的可布线性水平。每项为单次观测。

独立两轮弱开/关同路径诊断另见 `getrf-srl-share4-weak-attraction-analysis-20261012.md`：关闭组关键FF→BRAM由跨SLR变为开启组同SLR，net delay 9.589→2.130ns；但开启组也产生了新的高延迟关键路径，不能用原最差路径改善替代全设计结论。

## 最终DCP（只在服务器）

### eda099，独立两轮

- 弱关：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-max2-weak-false-10ns-full-20261011-235104-573533/reports/getrf_routed.dcp`
- 弱开：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-max2-weak-true-10ns-full-20261011-235104-573238/reports/getrf_routed.dcp`

### eda072，合并

- 弱关：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-merged-weak-false-10ns-full-20261011-235104-329263/reports/getrf_routed.dcp`
- 弱开：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-merged-weak-true-10ns-full-20261011-235104-342144/reports/getrf_routed.dcp`

每组同目录的 `getrf_placed.dcp` 为布局后DCP。最终DCP SHA256按表顺序：

1. `cf01340dbf69f3b8feab04ed6aa543fea4199d1231d226ccfb64215048ce1443`
2. `df769d3b499389cb65e9b290d4bf0fdab1860a83f57c3124ba889bfd9de2a6be`
3. `0e76078f04da6cb67c139d6ff762b3b2abbf6f7ffa426f1bd1809f4778893725`
4. `c1fbac0697ce9ae26f4a7c7bec209bafae4ce171adb960c835ab960cc44b748d`

原始证据为各run的manifest/status、reports/stages.tsv、amf_profile.tsv、timing_summary.rpt、utilization.rpt、route_status.rpt、各项audit JSON，以及logs/vivado.log。源码、冻结二进制、输入哈希与原码保护记录见各服务器实验父目录launch/environment/input-hashes/source-protection文件。
