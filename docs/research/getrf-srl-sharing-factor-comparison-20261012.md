# GETRF：弱牵引、阶段组织与 SRL site 共享的对照结论

2026-10-12 核对两台服务器原始 manifest、config、最终 timing summary、Vivado 日志和主线程函数计时。

**当前最值得保留的组合是：普通 Pro、独立 SLR 最多两轮、开启弱牵引、每个 SLICEM 最多容纳 4 个兼容 SRL。** SRL 共享对资源需求和 MCLB 效率的改善最明确；弱牵引的收益依赖于共享与阶段组织，不能概括为始终有效；合并方案在当前参数下没有体现综合优势。

**对照选择与口径**

- 八组均为 GETRF/U250、10 ns、普通 Pro、关闭延迟特例、无外部 floorplan；AMF 8 线程、Vivado 2026.1 4 线程、repair 导入。AMF 九类输入 SHA256 与输入 DCP SHA256 在八组间一致。
- 旧弱关：10 月 11 日 16:13 的 pro-stage-legalization-study；旧弱开：20:28 的 mclb-recompute-efficiency 中 coupled 组。没有使用 own-distance 按需重算组作对照。
- 旧弱关尚未解耦开关：DSPCritical=true 使距离不超过 6 的弱分支返回 0；新显式 BoundaryWeakAttraction=false 与此分支等价。解耦后开启弱分支，不改变 DSP 时序设置。弱关仍保留距离大于 6 的强牵引。
- 共享版撤回按需重算，保留独立 PU，只在合法化中构造兼容共享组进行匹配，再把 site/目标回写每个原 PU；最终打包保存所有成员。不是初始打包合并 PU，也不是单纯将数值容量从 1 改为 4。
- 同阶段历史与本轮在同一服务器：独立两轮在 eda099，合并在 eda072。阶段间墙钟是实际运行结果，不作为消除机器与并发影响后的纯算法加速比。旧弱开版本还带有重算有效性诊断，虽然 coupled 下不复用目标，因此旧/新绝非逐字节同一二进制。
- WNS 为最终 timing_summary 的值。全局迭代含首次 Global Iteration 0；八组均只调用一次 route_design，内部全局迭代不是多次完整 route_design。Phase 5 额外 hold 迭代单列。

**完整结果**

时间单位为分钟，HPWL 单位为百万个当前加权坐标单位。“独占”指每个 SRL 类宏独占 site，并非一个 SRL primitive 占一个 site；已有级联宏本来可以包含多个 SRL。

| SRL策略 | 阶段 | 弱牵引 | WNS/ns | AMF进程/min | route/min | 全局迭代/额外hold | 初期timing拥挤 | SLL | 加权HPWL/百万 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 旧：宏独占 | 独立两轮 | 关 | +0.133 | 30.01 | 19.69 | 1/1 | 5 | 12310 | 4.1516 |
| 旧：宏独占 | 独立两轮 | 开 | +0.138 | 44.27 | 23.46 | 1/1 | 5 | 12769 | 4.3250 |
| 旧：宏独占 | 合并 | 关 | +0.120 | 42.57 | 20.70 | 2/0 | 6 | 8984 | 5.1128 |
| 旧：宏独占 | 合并 | 开 | +0.002 | 61.87 | 36.47 | 2/0 | 7 | 12355 | 5.4639 |
| 新：共享≤4 | 独立两轮 | 关 | +0.035 | 21.87 | 22.41 | 2/0 | 5 | 10332 | 3.9971 |
| 新：共享≤4 | 独立两轮 | 开 | +0.160 | 21.71 | 22.29 | 1/1 | 5 | 11656 | 4.1440 |
| 新：共享≤4 | 合并 | 关 | +0.016 | 26.78 | 41.25 | 4/0 | 7 | 10110 | 5.1987 |
| 新：共享≤4 | 合并 | 开 | +0.029 | 27.12 | 29.75 | 3/0 | 7 | 10323 | 5.1402 |

八组均完整布线、TNS=0、setup/hold 无违例、DRC Error/Critical Warning 为 0。新四组均保留全部 856998 个 AMF 请求实例的原始 LOC/BEL，12183 条专用级联、325 条 SRL 级联合法；仍称 repair 策略，不改称 strict。

**1. 放宽 SRL 排他性：效率上明确有效，时序上依赖组合。**

12431 个 SRL primitive 原先属于 12231 个 SRL 类宏；共享后只使用 3171～3179 个 SLICEM site，减少约 74%。这不是整设计全部 SLICEM 占用下降 74%，PU 密度权重本轮也没有按此比例改变。共享分组减少了进入合法化匹配的独占对象与 site 竞争，是直接且符合实现的收益。

| 对照（旧→新） | GP函数/min | GP宏合法化/min | 其中MCLB/min | MCLB调用次数 | AMF进程下降 |
|---|---:|---:|---:|---:|---:|
| 独立两轮/弱关 | 22.27 → 13.22 | 15.00 → 5.17 | 11.27 → 0.60 | 29 → 29 | 27.1% |
| 独立两轮/弱开 | 35.39 → 13.35 | 27.11 → 4.94 | 22.38 → 0.63 | 29 → 29 | 50.9% |
| 合并/弱关 | 31.62 → 17.07 | 22.76 → 7.49 | 18.74 → 1.10 | 31 → 31 | 37.1% |
| 合并/弱开 | 46.97 → 17.35 | 34.62 → 7.62 | 28.54 → 1.11 | 31 → 31 | 56.2% |

MCLB 累计耗时下降 94.1%～97.2%，调用次数保持不变，因此不能解释为少做或跳过合法化。GP 包含宏合法化，宏合法化包含 MCLB，这些 inclusive wall 时间不能相加；GP 使用 GlobalPlacement_CLBElements 主线程函数，旧文档按 phase-completed 日志求和的 22.20 min 与这里 22.27 min 是口径差异。

时序并非四组都改善：独立两轮/弱关从 +0.133 降为 +0.035 ns；合并/弱关从 +0.120 降为 +0.016 ns，且 route 从 20.70 增至 41.25 min。两组弱开则分别 +0.138→+0.160、+0.002→+0.029 ns。由此支持保留共享实现用于降低资源与计算压力，但不支持“共享本身必然改善时序/布线”的结论。共享改变匹配对象、合法目标及最终分布，会改变关键路径；现有数据没有把其每一种物理影响独立分离。

**2. 弱牵引：共享后的独立两轮有明显收益；历史上并不普遍有利。**

| 固定条件，只比较弱关→弱开 | WNS/ns | AMF进程/min | route/min | 判断 |
|---|---:|---:|---:|---|
| 旧独占、独立两轮 | +0.133→+0.138 | 30.01→44.27 | 19.69→23.46 | 仅5 ps余量收益，不足以支持效率代价 |
| 旧独占、合并 | +0.120→+0.002 | 42.57→61.87 | 20.70→36.47 | 本次观测明显更差 |
| 新共享、独立两轮 | +0.035→+0.160 | 21.87→21.71 | 22.41→22.29 | 当前最有价值的正面结果 |
| 新共享、合并 | +0.016→+0.029 | 26.78→27.12 | 41.25→29.75 | 布线耗时改善，但总体仍不理想 |

共享后的独立两轮，弱开使全局布线 2→1 轮，初期 timing congestion 保持 5；代价是 SLL 10332→11656（+12.8%）、HPWL +3.7%。说明局部关键路径改善不要求全设计总跨 SLR 用量同时下降。

已完成同端点 DCP 诊断：弱关最差 FF→BRAM 连接在开启后由跨 SLR 变为同 SLR，net delay 从 9.589 降至 2.130 ns；但 FF、BRAM 横向位置和整棵高扇出树也变了，不能将差值全部称为固有跨 SLR 延迟。开启组产生新的 FF→URAM 瓶颈，也有路径变差。此证据支持弱牵引改变了关键逻辑分布，并非只是 Vivado 日志差异；不证明所有路径都改善，也不能把一轮最佳 WNS 当作统计上的稳定收益。

**3. 阶段组织：当前独立两轮更合适，合并没有体现综合优势。**

在“独占/共享 × 弱关/弱开”的四对结果中，独立两轮都取得了更高 WNS、更短 AMF 与 route 时间、更低 timing congestion。共享后的合并组仍是拥挤等级 7、全局布线 3～4 轮，独立两轮是等级 5、1～2 轮。合并消除了额外阶段预算，却没有得到较短的真实总耗时，也没有消除后端局部拥挤。

共享后 MCLB 仅 0.60～1.11 min，合并版的布线仍明显更差。这说明“合并差只因 MCLB 匹配太慢”不足以解释现象；SLR-Y 与 HPIO-X 同时作用时的连续布局轨迹、密度与宏目标关系仍需要另外验证，不能从当前八个终点结果指定唯一根因。

合并的一个代价交换是 SLL 较低，例如共享/弱开 10323，独立两轮/弱开 11656；但前者 WNS 仅 +0.029 ns，route 29.75 min、全局迭代 3 轮，HPWL 也更高。按用户“时序为目标、可布线性/效率为软约束”的优先级，当前不足以推荐合并。

**建议使用的候选基线与边界**

独立两轮 + 弱牵引开 + 共享≤4：正式 WNS +0.160 ns，AMF 21.71 min、GP 13.35 min、GP 宏合法化 4.94 min（其中 MCLB 0.63 min）、最终 packCLBs 6.66 min、route 22.29 min，timing congestion=5、全局迭代1次，SLL=11656、HPWL=4143975.932820。packCLBs 含补充合法化及详细布局，不能把整个函数都当成合法化时间。

与相同独立两轮/弱开的旧独占组相比：AMF -50.9%、GP -62.3%、route -5.0%、SLL -8.7%、HPWL -4.2%，WNS 增加22 ps。与更快的旧独立两轮/弱关参考相比：AMF -27.6%、GP -40.1%、SLL -5.3%、HPWL -0.18%，WNS +27 ps，但 route **增加13.2%（19.69→22.29 min）**，所以不是所有时间指标都全面胜出。

每个配置目前只是一轮观测，三个因素存在明显交互；不能将八组混合平均后宣称每个因素具有独立、稳定的收益。效率与 site 数的改善证据强于几十 ps 的 WNS 优劣证据。建议把上述组合作为下一轮普通 Pro 基线候选，保留旧独立两轮/弱关作为较好可布线性的历史参考；不由此断言其他设计或周期也有相同排序。

**可追溯数据**

结构化证据：experiments/comparisons/20261012-srl-sharing-factor-comparison/evidence.json，保存本次读取的配置、输入哈希、时序、计时、迭代标记与八个服务器 run 路径。

- old-max2-off，eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-pro-stage-legalization-study/repo/experiments/runs/getrf-pro-stage-legalization-max2-10ns-full-20261011-161320-690366`
- old-max2-on，eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-mclb-recompute-efficiency/repo/experiments/runs/getrf-mclb-max2-coupled-weak-10ns-full-20261011-202838-452495`
- old-merged-off，eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-pro-stage-legalization-study/repo/experiments/runs/getrf-pro-stage-legalization-merged-10ns-full-20261011-161318-439166`
- old-merged-on，eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-mclb-recompute-efficiency/repo/experiments/runs/getrf-mclb-merged-coupled-weak-10ns-full-20261011-202831-896347`
- new-max2-off，eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-max2-weak-false-10ns-full-20261011-235104-573533`
- new-max2-on，eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-max2-weak-true-10ns-full-20261011-235104-573238`
- new-merged-off，eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-merged-weak-false-10ns-full-20261011-235104-329263`
- new-merged-on，eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261011-srl-share4/repo/experiments/runs/getrf-srl-share4-merged-weak-true-10ns-full-20261011-235104-342144`

各 run 的 reports/getrf_routed.dcp 和 reports/getrf_placed.dcp 为对应布线后、布局后 DCP，默认仅在服务器保存。资源审计与最终验收详见 getrf-srl-share4-final-results-20261012.md；同路径诊断详见 getrf-srl-share4-weak-attraction-analysis-20261012.md。
