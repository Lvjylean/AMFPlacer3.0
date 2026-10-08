# GETRF：FF 空间分布与外部 floorplan 偏离诊断（2026-10-07）

本报告对比相同外部 floorplan 初始化下的旧边界策略成功轮次与 paper-hierarchical 失败轮次。分析只读取历史 AMF 输出和位置快照，未修改算法、历史产物或正在运行的无 floorplan 实验；定时检查保持暂停。

## 结论与证据等级

已证实：新轮次在 AMF 全局布局阶段显著扩展到 SLR2/3，最终打包不是大范围迁移的起点。外部 floorplan 初始化基本正确，但其 membership 与 CR 集合只用于初始化，没有在后续目标函数中保留持续的分区软偏好。新边界策略维护另一套长路径 cluster 的物理区域中心吸引；这两套分区并不相同。容量检查只覆盖区域内的逻辑/硬资源，不覆盖 SLL 接缝连接容量。

代码与结果支持、但尚未通过消融实验独立确证的解释：对部分长路径逻辑持续施加整片 SLR/HPIO 半区中心吸引，会改变它与未直接受引导 FF 的相对分布；普通密度扩散可跨这些区域，最终打包再处理由此产生的局部拥挤。不能从现有快照把每个 FF 的最终位置唯一归因于某一项力，也不能声称某一行代码单独造成全部退化。

## 轮次、DCP 与工具阶段

- 旧策略：getrf-external-floorplan-10ns-vivado2026-1-full-20261007-013540-091549。
- 新策略：getrf-paper-boundaries-memfix-10ns-vivado2026-1-full-20261007-141804-694449。
- 两轮均为 GETRF、10 ns、外部 15 分区初始化、AMF 8 线程、Vivado 2026.1 / 4 线程；新轮次包含此前仅修复对象释放的内存修复，不能把两份程序称为同一二进制。
- 比较的是 AMF requested.tsv 中的同一批 856,998 个真实输入实例。新轮次的导入审计确认 LOC/BEL 100% 保留。旧轮次导入及最终位置保留已经通过其独立验收。
- 用户截图对应的失败状态是 AMF 布局导入后、Vivado place_design 前置检查失败，尚未执行 Vivado 布局优化或布线。诊断重建 DCP：
  /Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/experiments/preflight/20261007-paper-slrc-import-probe-01/reports/getrf_imported.dcp
- DCP SHA256：b0ab0958949d7f809de9e8103813c9c72f10c17d8aaee3021cfa394f9645efcc。DCP 留服务器。
- 额外只读流程诊断在内存设计中解除 856,998 个导入实例的固定标志后，SLRC Error 数仍为 1；未执行新的 place/route。这说明仅解除这些固定标志不足以解决当前检查失败，不是已经得到另一份合法实现。

## 外部规划的遵从度与扩散

外部初始化时 856,127 / 856,998（99.90%）真实实例在各自规划 CR 集合内，871 个在外；没有资源类别回退（resource_fallback_pus=0）。宏偏移/设备边缘修正仍可能使少数成员在外。

| 指标 | 旧策略 | 新策略 |
|---|---:|---:|
| 位于自身规划 CR 集合之外的实例 | 248,418（28.99%） | 417,051（48.66%） |
| 位于自身规划 SLR 之外的实例 | 102,404（11.95%） | 221,918（25.89%） |
| SLR0 实例 | 407,867 | 405,605 |
| SLR1 实例 | 448,821 | 307,014 |
| SLR2 实例 | 310 | 143,185 |
| SLR3 实例 | 0 | 1,194 |
| AMF 最终 HPWL | 3,500,027.622 | 11,789,874.258 |

新 HPWL 为旧轮次的 3.37 倍。这里“规划之外”是偏离统计，不是把软约束当作硬合法性违规。

准确地说，原始规划主体位于 SLR0/1，S14 单独包含 SLR2 的 X3Y8（6,933 个实例），所以不能要求 SLR2 绝对为零。新轮次的大规模溢出却主要来自应位于 SLR1 的 S01：该分区最终有 133,095 个实例进入 SLR2，262 个进入 SLR3；共有 203,227 / 504,505 个离开其规划 SLR。旧轮次 S01 在 SLR2 仅 310 个。

同一接缝的静态网表统计从 135 增至 54,440 个跨界不同 net；新轮次跨 SLR1–2 的 185,984 条 driver→sink pin 连接中，172,396（92.69%）属于 S01 内部。不是仅有少量合理的模块间边界连接。静态不同 net / pin 连接计数不能冒称 Vivado 路由后 SLL 使用量。

## 扩散发生在哪一步

FinalLUTFF-0 至 -4 是五次 GlobalPlacement_CLBElements 调用结束时输出，均在最终 ParallelCLBPacker 打包之前。按原始网表名字过滤虚拟实例，五份快照均覆盖全部 391,184 个 FF 和 407,748 个 LUT1–6。连续坐标按模型的 y=239.5/479.5/719.5 切分 SLR；最终采用精确 site 元数据。

| 新轮次阶段 | SLR2/3 中真实 LUT+FF 数 |
|---|---:|
| 第一段全局布局结束（约 617.5 s） | 17,422 |
| 第二段结束（约 919.0 s） | 50,460 |
| 第三段结束（约 1233.8 s） | 128,763 |
| 第四段结束（约 1500.9 s） | 116,497 |
| 最终打包前（约 2089.7 s） | 131,952 |
| 最终 AMF 导出 | 131,942 |

旧轮次打包前相同统计仅 324 个，最终 291 个。新轮次打包前后净人数相近不代表没有个体交换：最后快照到最终仍有 1,750 个 FF、881 个 LUT 改变 SLR。但主要扩散已发生在全局布局。

## FF 是否“全部聚在边界”

实际 FF 数为 391,184。为避免仅凭截图亮度判断，采用固定诊断窗口；距离单位为当前 AMF 映射坐标，不是微米，也不是硬件对边界区域的定义。下表是 FF 数量与占所有 FF 的比例，并非位置占用率或拥塞率。

| 窗口 | 旧策略 FF | 新策略 FF |
|---|---:|---:|
| 任意 SLR 接缝上下各 10 个 Y 单位 | 18,963（4.85%） | 33,382（8.53%） |
| 其中 SLR0–1 接缝 | 18,352 | 25,640 |
| 其中 SLR1–2 接缝 | 611 | 7,585 |
| HPIO 中线 x=74.75 左右各 5 个 X 单位 | 29,240（7.47%） | 25,331（6.48%） |
| 上述两类窗口并集（不重复计数） | 46,891（11.99%） | 57,119（14.60%） |

因此 SLR 邻近带的 FF 增多有数据支持；“全部 FF 都挤在 SLR/HPIO”不符合计数，HPIO 邻近带按此口径也没有增加。不同带宽会改变数字；截图中的连线叠加和颜色选择不能替代逐实例计数。

## 实现中需要区分的四件事

### 1. 初始位置并不是持续的 floorplan 软偏好

ExternalFloorplan.cc 读取 membership 与 CR 集合，选择资源匹配落点，对 PU 执行 setAnchorLocationAndForgetTheOriginalOne，再更新 bin；输出明确声明 persistent_region_constraints=false。局部 module/CR 映射没有传给后续目标函数作为持久偏好。

这符合此前约定的“只替代初始化、不永久锁区”，但无法继续表达“在资源允许时尽量维持原分区范围”。持续软偏好与硬锁区是不同机制。旧策略也已有 28.99% 实例偏离自己的 CR 集合，说明问题不全是本轮新引导引入的。

### 2. 新聚类对象与外部 partition 不同，并只覆盖部分逻辑

BoundaryAwareClusterer_Paper.cc 从最长路径前 10% 种子建立 cluster，沿未注册的长组合逻辑遍历；遍历不会一般性地越过寄存器。宏整体保留，可能携带其中的 FF，但这不等于整条寄存器到寄存器锥和外部分区一起移动。

每轮接受 274 个 cluster / 74,603 个 PU。日志中 cluster 真实实例计数合计 106,955，但未针对未被 claimed 的硬资源去重，因此不将该数作为独立覆盖率，更不能将其误作直接加锚点的 FF 数。DSP/BRAM/URAM 可以参与投票，但不会被这套 movable() 逻辑直接锚定。

### 3. 当前中心吸引不只针对跨界实例

PlacementInfo::regionAnchor 将目标设为所选 SLR×HPIO 半区的几何中心（含宏偏移修正），不是把目标设在接缝上。WirelengthOptimizer::updatePseudoNetForClockRegion 对每个受引导 PU 加入二次伪网项，即便它已经位于所选区域内也会继续吸引。

对单个轴 d，第 k 次 QP 中可写为：
E_i,d = 1/2 * w_i,d * (p_i,d - a_i,d)^2，
w_i,d = beta * max(1,真实引脚数) * abs(a_i,d - p_i,d^(k))。
这里忽略了与该项无关的常数；本分支传入 y2xRatio=1，两个轴独立按各自位移加权。其他普通连线、扩散位置和宏合法化项仍同时存在。

对于普通单 cell，SLR0/1 的 Y 中心是 119.5 / 359.5；HPIO 左右半区的 X 中心约为 37.25 / 113。第一轮 274 个 cluster 中已有 272 个完全在各自选定 SLR 内；第二轮 274 个全部已在选定 SLR 和 HPIO 侧，仍继续添加中心吸引。这是值得优先检查的过度中心化机制，不能把它只描述为“把跨 SLR 的少数节点拉回来”。

一个用于理解的简化例子：若未受直接区域引导的 FF 同时连接位于 y=119.5 和 y=359.5 的两团逻辑，等权二次连线项的平衡点是 y=239.5，恰好为 SLR 接缝。这只是说明为何没有“边界锚点”也可能产生边界聚集，不是证明所有实际 FF 都由这种两端等权关系造成。

从第三次聚类日志看，只新选择了 6 个 PU 到 SLR2，不能把最终 14 万余实例在上方 SLR 的结果解释成它们全部被直接指定了上方区域。

### 4. 密度、打包和连接容量不是同一项约束

GeneralSpreader.cc 明确允许物理区域偏好被密度扩散越过，未依据外部 module 的 CR 集合限制扩散。paper_spreading.tsv 中新增的显式边界 spreading 调整很小且检查结果仍位于原物理 region 内；它不是直接把 14 万实例搬到 SLR2 的操作。应重点分析中心锚点、普通密度扩散、时序/宏项之间的平衡。

最终打包前还会清除 transient region preferences；CLB packing/补充合法化不含外部分区保持代价。因此局部打包困难不能依靠原 floorplan 自行恢复。

RegionCapacityTracker 检查 LUT/FF/MLUT/CARRY/MUX/DSP/BRAM/URAM 等 site 资源，不检查接缝上的 SLL 容量。STA 中跨 SLR 的延迟罚值也不等价于 SLL 数量约束。最终 Vivado 报至少 54,246 个 SLR1–2 连接需求，而容量 23,040，约为 2.35 倍。仅有区域“装得下”检查不足以预防该失败。

## 对 6.5 ns / 10 ns 的理解与修正方向

用户提供的其他 6.5 ns 实验是重要可行性线索；其输入/器件/约束是否完全一致，尚未在本轮诊断核验。已有同设计外部初始化的 10 ns 旧策略成功结果本身就表明，不能直接把本轮向第三个 SLR 的扩张解释成必需的资源需求。时钟周期不是面积上限，当前代码也不会自动把较宽松周期转换成“遵从较小 floorplan”的目标。

建议先修正模型含义，而非直接加大 SLR 延迟常数：
1. 对外部分区保留持续但可违反的区域偏好：原 CR 集合内零额外位置代价、越界付出有限代价；按宏整体处理，不强迫模块内所有实例挤到一个中心。
2. 检查 SLR Y 引导的触发和退出条件、覆盖范围与强度，保留论文的聚拢目的，避免对已经解决跨界问题的 cluster 无差别持续压向整片 SLR 中心。具体改法需按用户对原论文策略的要求单独确定。
3. 在聚拢、扩散和最终打包之间一致维护软偏好，并诊断每个 SLR 接缝的 net 需求与容量；不能用 LUT/FF 容量预留替代 SLL 检查。
4. 保留当前无外部 floorplan 对照实验不变，区分新聚拢策略本身的影响与它和外部初始化的相互作用。单轮对比不构成严格因果证明。

本次只新增诊断脚本与报告，未实施上述算法修改。

## 可复核文件

独立副本中的源码位置：
- src/lib/HiFPlacer/placement/globalPlacement/ExternalFloorplan.cc:307
- src/lib/HiFPlacer/placement/placementTiming/BoundaryAwareClusterer_Paper.cc:56
- src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc:2282
- src/lib/HiFPlacer/placement/globalPlacement/WirelengthOptimizer.cc:1093
- src/lib/HiFPlacer/placement/globalPlacement/GeneralSpreader.cc:434
- src/lib/HiFPlacer/placement/placementInfo/RegionCapacityTracker.cc:26
- src/app/AMFPlacer/AMFPlacer.h:440

统计脚本：scripts/diagnostics/audit_floorplan_spatial_distribution.py。
结构化证据：experiments/evidence/20261007-paper-boundaries-full-01/spatial-audit-01/audit.json、cluster-summary.json。
连接统计：同 evidence 下 slrc-failure-01/static-connectivity.json。
固定标志诊断：experiments/preflight/20261007-paper-slrc-import-probe-01/reports/fixed_state_probe.json。
