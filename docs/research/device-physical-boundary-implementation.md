# 器件物理边界与二维聚拢实施记录

2026-09-27。服务器根目录 `/Projects/jinyang/workspace/AMFplacer3.0`，分支 `codex/device-physical-boundaries`。

A–D 已实现并完成 U250 / GETRF 三组完整验证，最终结果见 [验收报告](device-physical-boundary-getrf-validation.md)。三组全量布通、DRC/级联检查通过；仅边界延迟版达到 10 ns 时序，二维聚拢版优于 control 但未优于仅延迟版，继续保留实验开关。默认配置和 `builds/validated-getrf-u250-full` 不变。本文以下保留实施过程、修复和失败轮次，历史“运行中”描述不代表最终状态。

## A：器件结构与模型

- 完整导出 346,675 个 site、781,860 个 tile，以及 clock-region/SLR/I/O bank 关系；空 bank 查询缺项原样记录。
- `data/devices/u250-physical-v1/raw/` 保存原始文件，`regenerated-fabric.coordinates.json` 保存 RPM→AMF 映射，`model/` 保存 TSV、审计 JSON 与区域 SVG。
- 新旧 fabric 解压后 SHA-256 均为 `637d7177a44c6276323ab0f0393899a89c1b70fdff45816c1bbf84adb2e8eec2`，原器件库未修改。
- AMF 坐标中 SLR 接缝位于 Y=239.5、479.5、719.5；HPIO 带位于 X=158（RPM_X=2576、tile COLUMN=369、clock-region X4）。这里的列号是导出结果，不是分类条件。
- 4 个 SLR 各分左右两区，共 8 区。容量分别统计 LUT、FF、SLICEM LUT、CARRY、MUX7/8/9、DSP、BRAM18/36、URAM；MLUT 是 LUT 的子集，BRAM18+2×BRAM36 共享 18K 槽位。
- CONFIG/PCIe/CMAC 候选保留位置但不加罚：现有证据不能证明连接必须横穿其局部范围。PLL_SELECT_SITE、BIAS、HARD_SYNC、MMCM、PLL、ILKNE4、CFGIO_SITE 等未覆盖类型被明确报告。
- 当前原生几何支持纵向堆叠 SLR 与贯穿 I/O 带；其他拓扑、其他器件资源类型需要验证或补充规则，不能据此宣称任意多 die 器件已兼容。

模型 SHA-256：`8620b87b7469bbede1c5d5f3818ab9a2011a7e940386ab2a6b7906aaf6152cdb`。入口核验模型、fabric、坐标、规则和原始导出哈希；C++ 核验 part、site 覆盖、坐标、SLR、禁用属性和区域归属。

## B：统一延迟与后端测量

新模式保留原三个距离回归区间，移除固定 X 列经验项，以统一模型添加 1.5 ns/SLR 接缝和 0.5 ns/HPIO 完整穿越。普通 clock-region、DSP/BRAM/URAM 列没有额外固定罚项。两种 `getDelayByModel` 入口一致；`SLRBoundaryDelayNs=0` 可消融 SLR 项。关闭新模式完整保留旧延迟行为。

从既有成功 routed DCP 采集最多 200 条受约束 setup 路径的连接延迟。脚本排除时钟、常量和缺失路由样本，读取 `NET_DELAY.SLOW_MAX`（ps 转 ns），按距离、扇出、资源类型与跨界组合报告相对于原距离模型的残差。几何使用 cell site；不是精确 pin 几何、真实路由轨迹或 SLL 使用量。缺样和查询错误有独立报告，不据样本自动修改系数。

首次抽样已完成：200 条路径、370 条唯一连接、缺失 site/查询缺项均为 0，目录 `experiments/preflight/boundary-routed-samples-20260927-01/`。最差路径复现 WNS −2.886 ns、datapath 12.873 ns、6 次 SLR 穿越（3 次抵消方向的往返）及 1 次 I/O 带穿越。系数仍为启发式初值，不能把所有绕行/负载残差归因于边界。

## C：容量预算与二维软目标

- 从 STA 的负 slack/近临界端点提取有限路径段，PU 重叠采用确定性归属。
- 移动 CLB PU；DSP、BRAM、URAM、固定/锁定单元参与连线评分但不由聚拢器直接搬移。原有硬资源合法化仍工作。
- 候选为当前/同 SLR 另一侧/相邻 SLR 区域；检查所有受影响输入、输出和内部 timing edge。候选试评用当前 arrival/required time 的局部延迟替换，并非每个候选重新执行完整 STA；移动后重新 STA 并释放有害偏好。
- 区域预算扣除禁用 site、当前实际占用与已接受簇预留，允许移出过载区，拒绝重复预留和类型容量不足；容量是必要条件，最终仍须打包及合法化。
- 区域外 PU 指向最近可行 SLICE/SLICEM；区域内不拉到中心。X/Y 按需分别添加软吸引。摊开可以离开目标区；站点偏好只排序，不硬过滤，搜索有界。
- LUT/FF 配对和最终打包前清理旧目标；刷新时先验证 PU 生命周期，再重算预算；固定或锁定 PU 不生成移动目标。
- 新模式绕开旧 VCU108 固定列裁剪和整列 Y 拉伸；没有重写初始 PaToH/SA 或硬宏匹配的全局目标。

首轮参数：512 簇、128 cell/路径段、8192 受影响边/候选、近临界阈值 0.15×目标周期、最大位移 280（X+y2xRatio×Y）、最小收益 0.05 ns、位移成本 0.0001。每轮记录生效配置。

## 验证与构建

- 实现提交 `c54011dd`；审计脚本/进度修正 `9ddf809f`（C++ 不变）。
- RAMB36 容量修正 `b64ea47d`；阶段/关键路径审计 `acc3123f`。
- 冻结二进制：`builds/build-20260927-031245-945274-acc3123f/build/AMFPlacer`。
- 54 项 Python 测试通过，服务器日志 `experiments/preflight/physical-python-tests-20260927-final.log`。
- 原生物理模型：接缝中点、普通 CR、I/O 端点、XY 叠加去重、SLR 消融、合法站点、超大宏、容量竞争/释放、SLICEM 子集和 BRAM 重叠通过。
- 原生时序 15 组/157 行：`experiments/preflight/boundary-timing-regression-20260927-05/`，包括真实 U250、SLR ID 重排、非法系数、VCU108 旧模式和新模式两种接口。
- 原生聚拢 7 组：`experiments/preflight/boundary-clustering-20260927-05/`，包括 SLR、I/O、XY、区域内不聚中心、带寄存器 DSP、URAM、无有效发射起点；还覆盖 RAMB36 虚拟上半槽去重、失效/锁定 PU 清理、软区域回退及站点耗尽时的有界退出。修正后的探针位于 `experiments/preflight/boundary-native-probe-20260927-05/`，链接冻结构建的生产静态库，记录源码、库和二进制哈希，不修改构建快照。
- 早期失败测试保留：01 缺少测试 fixture 的 cell→PU 映射；02 的 DSP fixture 未声明寄存器，暴露旧 STA 无有效路径时的越界。已修正 fixture 并增加空路径保护，不伪装成成功轮次。

## D：完整 GETRF 对照

早期比较已中止并保留：`experiments/comparisons/getrf-physical-20260927-030230-391597/`。约第 5 分钟的真实 GETRF 容量报告暴露 RAMB36+虚拟 RAMB18 双计；已修正并完成回归，下表仅列历史轮次。重新对照的 ID 在验收记录中登记。

| 组 | 配置 | 运行目录后缀 |
|---|---|---|
| control | getrf-u250-physical-control.json | getrf-u250-full-20260927-030230-490881 |
| delay | getrf-u250-physical-delay.json | getrf-u250-full-20260927-030230-491096 |
| cluster | getrf-u250-physical-boundaries.json | getrf-u250-full-20260927-030230-487935 |

三个目录均在 `experiments/runs/`。同一 post-opt DCP、补齐时钟的网表、10 ns、同一二进制、AMF 8 线程/Vivado 4 线程；三组并行，观察耗时受到 CPU/内存竞争影响，不能作为独占资源下的严格速度结论。control 包含现有 1.5 ns SLR 修正，不是之前关闭 SLR 修正的历史基线。

`reports/physical/` 记录模型/参数、AMF STA 跨界计数、簇决策、区域预算、QP 的 X/Y 吸引数、导入/放置/布线后的 cell-site 跨界计数和关键路径样本。`placement/*_cell_sites.tsv` 留服务器，不进入轻量报告包。检查网表覆盖、级联、DRC、布线完整性、WNS/TNS、位置保留和各阶段耗时。

最终 DCP 仅保存服务器：`experiments/runs/<上述运行目录>/reports/getrf_routed.dcp`。文件实际生成及校验以前，不能将预计路径当作成功产物。

比较入口：

```bash
python3 scripts/amf3.py compare-boundaries \
  --binary builds/build-20260927-040853-711621-4a9afc6f/build/AMFPlacer \
  --parallel 3
```

每次调用都会创建新实验，查询已有轮次不要重复启动。`comparison.json` 在各组完成后汇总结果；`control.json`/`delay.json`/`cluster.json` 保存实时运行目录。新模式只有经过完整 QoR 比较后才能考虑作为默认；本次入口不会自动修改默认配置。

## 抽样解释

相对原距离回归的残差：无 SLR/I/O 穿越 207 个样本，中位数 −0.098 ns；仅跨 1 道 SLR 62 个样本，中位数 4.923 ns；仅跨 2 道 SLR 91 个样本，中位数 4.405 ns。这些来自最差 200 条路径，存在选择偏差、扇出和路由相关性，不能用整体中位数标定每道 SLR 代价。只跨 I/O 带且不跨 SLR 的独立样本只有 1 个，无法可靠分离 I/O 固定代价。40 个分层组保留于 `timing_sample_analysis.json`，正式系数继续使用获批初值 1.5/0.5 ns。

早期真实 GETRF 已确认 X/Y 吸引都触发：首轮接受 435 个路径段、580 个 PU；随后预算/时序刷新保留 15 个偏好，首轮 X/Y 各作用于 12 个 PU。这些是已中止轮次的过程证据，不是最终 QoR 结果。

## 修正后的 D 轮次

2026-09-27 03:20:31（服务器时间）启动，比较目录 `experiments/comparisons/getrf-physical-20260927-032031-506400/`，启动提交 `a71b61c9`，生产二进制对应 `acc3123f`。

| 组 | 完整运行 ID |
|---|---|
| control | getrf-u250-full-20260927-032031-594482 |
| delay | getrf-u250-full-20260927-032031-594456 |
| cluster | getrf-u250-full-20260927-032031-595260 |

该轮已因 control 数值失败停止，详见下一节。不得将这三个目录当作已完成验收。

## D 阶段数值稳定性阻塞与共同修复

03:20 对照 `getrf-physical-20260927-032031-506400` 已停止，不能视为完成。control `getrf-u250-full-20260927-032031-594482` 在约 2005.5 s AMF 阶段失败：HPWL 增至约 7.327e10，随后出现 NaN 坐标。delay 与 cluster 已保留现场并取消，尚未生成最终 DCP。

失败 control 的 `sta-20` 最差 slack 约 −108.598 ns，原嵌套幂公式在 `ClockPeriod=10`、`slackThr=-10`、`slackPowerFactor=1.1` 下得到约 3.1e38 的强化倍数，接近 float32 最大值；并非测得该单项已经超过上限。旧 QP 使用 float 累积对角元素，缺少有限性/求解状态检查，巨大权重会丢失小锚点项并可能在累加时溢出。

新增共同实验条件（仅三个物理边界对照配置启用，原标准配置保持不变）：

- `TimingMaxEnhancement=1000`：对数域限制强化倍数，覆盖嵌套幂、普通 LUT 配对与缓存平滑；默认 0 保留旧公式。允许 0 或 [1,1e6] 的有限值。1000 是本轮数值稳定性启发式，不是物理延迟系数。
- `QPStabilityGuard=true`：默认关闭。检查有限输入及对称弹簧矩阵；当浮点对角项不足时，补至非对角绝对值之和加 `max(1e-8,1e-7*offSum)`，同步修改线性项，新增弹簧锚定前次坐标。CG 候选非有限或正向增加修正后目标时显式回退；有限且目标下降的未收敛迭代允许使用，同时记录迭代数、残差、收敛标记、修复行和回退。非法输入由主线程报错，不在工作线程抛出未捕获异常。
- 三组使用同一冻结二进制及上述参数、10 ns 时钟与 SLR 1.5 ns；I/O 惩罚仍为 0.5 ns。故新 control 是“旧边界模型＋共同数值保护”，不是完全未经改动的历史基线。保留关闭开关复现原失败的入口。

新增原生 `checkQPStability` 覆盖普通权重/正常 QP 与旧模式一致、失败 slack、极端 slack、零阈值、参数拒绝、float 锚点丢失、线程错误传递与 NaN 结果回退。完整 GETRF 比较仍待重新运行；不能据此声称时序或布线已经改善。


## 数值保护初版对照与预检（已停止）

- 启动时间：2026-09-27 04:13:44（服务器时间）。
- 比较目录：`experiments/comparisons/getrf-physical-20260927-041344-459680/`。
- 启动提交：`9d92c0b6`；冻结生产源码：`4a9afc6f`，报告汇总增强：`b30bab9a`。
- 构建：`builds/build-20260927-040853-711621-4a9afc6f/build/AMFPlacer`。
- 二进制 SHA-256：`5d917544cff902b66c1ad5befe2af6d8666d5f7d8abf0df123c9b428b5483e17`。
- 56 项 Python 测试、原生物理模型、15 组/157 行原生时序、7 组原生聚拢及 `checkQPStability` 均通过。新预检目录后缀为 `20260927-guarded`，数值测试日志 `experiments/preflight/qp-stability-20260927.log`。

| 组 | 完整运行 ID |
|---|---|
| control：旧边界＋共同数值保护 | getrf-u250-full-20260927-041344-552357 |
| delay：新边界＋共同数值保护 | getrf-u250-full-20260927-041344-564377 |
| cluster：新边界＋二维聚拢＋共同数值保护 | getrf-u250-full-20260927-041344-557157 |

该 04:13 轮次在首个 QP 暴露稀疏矩阵逐项插入对角线的性能问题，已停止并保存。一次性 triplet 装配修复后重新运行，见下节。结束后同时审阅 `comparison.json`、各组 `status.json` 和 `reports/summary.json`，其中 `numerical_guard` 的累计事件数不是唯一 cell/net 数，必须同时检查回退和不收敛情况。默认配置和验证基线尚未晋升。


## 历史 D 对照：一次性稀疏矩阵装配（已停止）

2026-09-27 04:18:53 启动，比较目录 `experiments/comparisons/getrf-physical-20260927-041853-990282/`。生产提交 `7427f87b`；冻结二进制 `builds/build-20260927-041750-934343-7427f87b/build/AMFPlacer`，SHA-256 `14eeac6ed755c26099b121157030ca699433107e3165de80bc9d6408ab88ca74`。

| 组 | 完整运行 ID |
|---|---|
| control | getrf-u250-full-20260927-041854-078213 |
| delay | getrf-u250-full-20260927-041854-083355 |
| cluster | getrf-u250-full-20260927-041854-079412 |

仅数值保护矩阵装配实现变化：先加入全部对角 triplet，再一次压缩，不重复插入已压缩矩阵。新增十万变量测试与原数值测试整体耗时 0.02 s、峰值 RSS 25,388 KiB。前述边界/聚拢回归对应代码未变化。完整证据在 `experiments/evidence/device-physical-guarded-preflight-20260927/manifest.json`。

04:21 早期真实诊断每组已求解 20 次，无回退、无未收敛、无非法权重；最大对角修正约 0.0044。修复行计数包含无弹簧孤立行，按求解累计，不是唯一被移动 cell 数。此过程检查不代替最终布线验收。

## D 暴露的最终打包候选缺口

04:18 对照的 control/delay 已完成 AMF，分别 5459.6/5410.1 s，均覆盖 856,998 个 cell，均无 QP 回退；两组也完成了 Vivado 导入及导入审计。cluster 在最终打包中稳定剩余 3,774 个 PU，扩大搜索半径到 100 以上仍多轮不减。代码审计发现，前面的二维软目标已接通，但 `ParallelCLBPacker` 的普通/方向站点查询及 `PackingCLBSite` 的 PU 查询仍强制同一个 X 时钟区域列，遗漏了下游软回退接口。不能将前面的 C 预检当作完整打包验收。

补丁增加两个文件的修改：

- `src/lib/HiFPlacer/placement/packing/ParallelCLBPacker.cc`：只在 `BoundaryAwareClustering=true` 时取消普通 clock-region 列的候选硬筛选；只返回当前打包器实际持有的站点，防止 bin 中保留站点进入非法映射；连续 8 轮没有减少剩余 PU 时写出 `reports/physical/packing_stall.tsv`，包含 PU/cell 类型和坐标，不直接宣称设计不可放置。原终止界限保留。
- `src/lib/HiFPlacer/placement/packing/ParallelCLBPacker_PackingCLBSite.cc`：新模式的未映射/已映射 PU 邻域查询均允许跨普通 clock-region 列。物理时钟半列容量检查、固定站点和 cell/CLB 打包合法性继续执行。

新增 `src/tests/check_boundary_packing.cc`，用真实 U250 相邻时钟列验证普通查询、方向查询、未映射/已映射 PU 查询的四个分支，确认旧模式仍有原列筛选；再令目标列无可用 SLICE，仅邻列有站点，验证新模式实际完成跨列合法化。新增目标为 `checkBoundaryPacking`。

早期探针日志保留：API 非 const 引用调用修正；保留站点候选暴露并修正；实际合法化探针半径按既有环形搜索约定修正。新实验需要使用此补丁后的同一二进制重跑三组。此前两个已进入 Vivado 的轮次保留作诊断参考，不与新 cluster 混成“同二进制”最终对照。

该 cluster 旧进程于约 06:34 停止。新旧实验同时运行时，服务器实际内存占用约 100 GiB；为给新版三组留出资源，旧 control/delay 的 Vivado 进程于 06:38:41 主动停止。三个旧目录均保留取消原因和现场，不能将主动取消标成 Vivado 自发失败。U250 的所有 X 时钟列都有 SLICE，不能将此次问题归因为“空 I/O 时钟列”。

## 最终 D 对照：物理区域打包协同

2026-09-27 06:34:59 启动，比较目录 `experiments/comparisons/getrf-physical-20260927-063459-803375/`。启动提交 `49ac3e58`；冻结构建源码 `7a7b5d8b`，二进制 `builds/build-20260927-063221-904893-7a7b5d8b/build/AMFPlacer`，SHA-256 `47e7ba44fac7ac0c5499bbdb2500b100a47323d1a0477cd0b9622659d579ac17`。

| 组 | 完整运行 ID |
|---|---|
| control | getrf-u250-full-20260927-063459-912642 |
| delay | getrf-u250-full-20260927-063459-913333 |
| cluster | getrf-u250-full-20260927-063459-912759 |

三组同一冻结二进制，8 个 AMF 线程 / 4 个 Vivado 线程并行运行；用户时钟 10 ns、SLR 1.5 ns、I/O 0.5 ns，以及数值保护参数均保持一致。新构建通过 56 项 Python 测试及真实 U250 的 `checkBoundaryPacking`，日志 `experiments/preflight/physical-python-tests-20260927-packing.log`、`experiments/preflight/boundary-packing-20260927-04.log`。前述模型、时序、聚拢、QP 回归所覆盖的实现未再改变。

专项测试证明候选查询和跨列合法化已接通；是否消除 GETRF 的完整打包停滞、最终布线是否合法、QoR 是否改善，仍需本轮真实结果。若出现 `reports/physical/packing_stall.tsv`，立即按 PU/cell 类型和坐标诊断。证据清单保存在 `experiments/evidence/device-physical-packing-preflight-20260927/manifest.json`。

08:29 中间验收：三组均以退出码 0 完成 AMF，覆盖 856,998/856,998 个 cell，且进入 Vivado。cluster 的打包剩余 PU 从 7,053 降至 0（AMF elapsed 6402.792 s，搜索半径 62.4），随后完成详细布局；此前 3,774 PU 停滞已在真实 GETRF 上消除，没有产生停滞诊断。

| AMF 指标 | control | delay | cluster |
|---|---:|---:|---:|
| 进程耗时（s，包含导出与退出） | 5531.142 | 5467.488 | 6708.648 |
| 已分配 cell | 856998 | 856998 | 856998 |
| QP 求解 / 回退 / 未收敛次数 | 228 / 0 / 1 | 226 / 0 / 1 | 230 / 0 / 0 |
| 非法时序权重事件 | 0 | 0 | 0 |
| 最终 SLR 几何穿越次数 | 168362 | 148841 | 173747 |
| 最终 I/O 带几何穿越次数 | 91321 | 109638 | 83764 |
| 至少跨一种边界的唯一连接 | 229128 | 224909 | 221676 |

共同审计 2,664,613 条唯一 driver-sink timing edge。SLR/I/O 列是穿越次数，最后一列是去重连接数；不是 SLL 用量。cluster 的总体跨界连接较少，但 SLR 次数多于两个对照，因此不能只凭总计声称多 die 时序改善；仍以关键路径与完整布线后 QoR 验收。

## D 后处理时钟标识修复

首个完成的 control 报告暴露了统计口径问题：AMF 的 `clocks` 文件包含驱动引脚 `@PORT/ap_clk`，导出网表对应逻辑网名却为 `n568507`。旧后处理只按逻辑网名排除，导致时钟被混入缺少 site 的连接。提交 `7ba71b3b` 改为同时匹配驱动引脚标识，另行统计无 fabric site 的外部端口；真实缺少位置的数据单元仍作为缺项报告。新增回归覆盖外部时钟、内部 clock buffer、旧逻辑网名、普通外部端口、常量和真正缺少 site 的数据源，57 项 Python 测试通过。

control 的原报告已保存在 `reports/physical/audit-before-clock-filter-fix/`，修订记录为 `reports/physical/audit_revision.json`；只重算统计，没有重跑或修改布局/布线。修复后独立记录 407,718 条时钟连接和 9,020 条无 fabric site 的外部端口连接，布线后的 2,664,613 条数据连接均有位置。几何穿越计数、全部时序和其他布线字段保持原值。其余两组后处理使用同一修正版；报告内保存分析脚本与模型 SHA-256。该后处理提交与冻结 AMF 二进制 `7a7b5d8b` 分别追溯。
