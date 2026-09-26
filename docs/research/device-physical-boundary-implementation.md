# 器件物理边界与二维聚拢实施记录

2026-09-27。服务器根目录 `/Projects/jinyang/workspace/AMFplacer3.0`，分支 `codex/device-physical-boundaries`。

A–C 已实现并通过预检。早期 D 容量审计发现并修正了 RAMB36 虚拟占位重复计数，当前准备重新执行三组完整 GETRF。尚无新策略布线后的 QoR 结论，默认配置和 `builds/validated-getrf-u250-full` 不变。

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
  --binary builds/build-20260927-031245-945274-acc3123f/build/AMFPlacer \
  --parallel 3
```

每次调用都会创建新实验，查询已有轮次不要重复启动。`comparison.json` 在各组完成后汇总结果；`control.json`/`delay.json`/`cluster.json` 保存实时运行目录。新模式只有经过完整 QoR 比较后才能考虑作为默认；本次入口不会自动修改默认配置。

## 抽样解释

相对原距离回归的残差：无 SLR/I/O 穿越 207 个样本，中位数 −0.098 ns；仅跨 1 道 SLR 62 个样本，中位数 4.923 ns；仅跨 2 道 SLR 91 个样本，中位数 4.405 ns。这些来自最差 200 条路径，存在选择偏差、扇出和路由相关性，不能用整体中位数标定每道 SLR 代价。只跨 I/O 带且不跨 SLR 的独立样本只有 1 个，无法可靠分离 I/O 固定代价。40 个分层组保留于 `timing_sample_analysis.json`，正式系数继续使用获批初值 1.5/0.5 ns。

早期真实 GETRF 已确认 X/Y 吸引都触发：首轮接受 435 个路径段、580 个 PU；随后预算/时序刷新保留 15 个偏好，首轮 X/Y 各作用于 12 个 PU。这些是已中止轮次的过程证据，不是最终 QoR 结果。
