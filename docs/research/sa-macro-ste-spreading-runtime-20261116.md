# SA、宏吸收、SLL STE、扩散与耗时优化快照

对照范围
GitHub：Lvjylean/AMFPlacer3.0，硬件资源适配版本，57b7478003bed5661d9685f91f12bfa05bedcac8，2026-11-04。2026-11-16 已通过 git ls-remote 核对远程分支头。该发布说明对应主体适配代码 540d0077，不使用更旧的 main 作本次基线。
当前：17f71c506abb4fceb3b192d65418b99f196ac9e0，2026-11-15；099 程序 SHA256 ed40b659c038f3c9b659e5954fddf050a2c50f20c30e9e57bc7eb09f4a2998a8。就是本次前30帧预览及此前完整布局使用的版本。
直接比较两个独立 Git 历史中的源码内容。排除第三方源码，共66个源文件/测试文件变化。当前实验仓库只保留运行所需子集，缺少发布版的 docs/variants 等目录不记作算法删除。

一、packing 候选、计分和状态正确性
1. 修正邻域搜索的 bin 距离口径，使其采用与 cell 搜索一致的 y2xRatio；避免本可触达的候选被过早排除。
2. 区分竞争中的候选归属和真正已确定的站点归属；候选探索阶段不因临时归属而失去竞争资格。
3. 内圈候选可以补回；达到最大半径后的补搜有界复用。仅复用空间查询结果，位置、可用性或归属代次变化会重新扫描；合法性、候选评分和竞价仍按当前状态执行。
4. 只给去重后存活的候选计算分数，复用同一 seed 的不变评分项；新开普通 CLB 的串行竞价扣0.2，CARRY/存储/非CLB/MUX站不套这项扣分。
5. 主 packing、内部QP和兜底锁定入口 die，site-die缓存改用唯一DeviceSite指针；详细优化阶段依旧允许跨die。
6. 全局已接受的最佳布局与局部失败回滚分开保存；完整恢复站点状态和SLL规划，不让局部trial覆盖全局检查点。

二、CARRY虚拟预留及shape
1. 对独立次FF通路满足条件的行，省去不必要的虚拟FF2；并非取消所有半片/主FF预留。
2. CO所有接收端均为专用CARRY级联时，不再制造fabric输出所需的虚拟FF2；只要还有普通fabric接收端就保留需要的预留。
3. 输入DCP明确存在shape绑定、且当前行同时需要局部O消费与fabric CO出口时，把对应真实FF纳入CARRY宏并保住槽位，包含E/H以及已确认的O[2]→CFF。
4. CARRY/MUX站增加局部OUTMUX、直通和数据通路兼容检查。详细优化从源站移出PU后也检查剩余映射；映射试探/恢复采用事务，空映射不当作合法证据。
5. 曾尝试的动态CI且DI[0]为常零时放宽链首S[0] LUT进入A6LUT的修改已回退，不能列为当前启用策略。

三、RAM/SRL的资源利用
1. SLICEMFFPacking让合法的独立FF及FF对进入RAM/SRL所在站，按原语、行、主/次FF槽和真实网络匹配；保留控制组/极性要求。
2. RAM读输出与FF同行的受限槽补齐时钟兼容检查；不把该限制扩展到其他行的独立FF槽。
3. 普通LUT及LUT对可进入部分RAM空闲物理行，并允许同行FF在通路合法时共存；RAM32M16/RAM64M8已经占满的LUT行没有放宽。
4. SRL从固定数量4扩展为U250八行物理匹配。配置SRLMaxPerSLICEM=8表示八行模式，不是对所有SRL原语统一限制8个；合法双SRL16E能共行，SRLC32E仍按其真实占用。
5. 按SRL16E/SRLC32E及双SRL16E区分同行主FF/FF2所需的直连通路；站内先合法匹配，再给附近直连FF及FF对加偏好。存储FF对每个直连成员有2.5分，实际合法SRL同行槽另有5分偏好，受原搜索半径限制。
6. SRL.Q直驱FF不再提前组成限制它去同行槽的FF对/LUT-FF对；RAM32M16直驱FF只与同一RAM32M16的FF配对，普通FF的原配对机制保留。
7. RAM多宏共站实现保留，但当前配置缺省不提供RAM sharing controls file，因此本轮没有启用该试验。原SRL共站策略没有按后来讨论改成只偏好同A0–A3。

四、GP、SLL与扩散
1. 新增sllSTE：每条真实SLR边界同侧的数量项代价和牵引严格为0，只有真实跨界的net才产生代理梯度。线长项、时序项和其他牵引独立保留，不能理解为同die所有力为0。
2. 普通可移动单cell LUT/FF增加可配置锚点倍率；本轮全部GP外轮为5倍。最初6次初始化QP不在该外轮开关范围，宏/固定/locked/packed对象也不按普通单cell处理。
3. GPAlwaysLimitQPStep实现保留但本轮false：恢复原来的限步生效条件，没有把所有QP都强制限到10。
4. 粗5×5 FF网格容量先照顾虚拟FF基本预留，再对剩余容量应用原余量比例；虚拟需求没有删除，膨胀和硬准入没有一并放松。
5. 扩散改为容量驱动的长框、合框和分批：从1×1开始，按新增边净余量贪心长框，必要时廉价看第二层；较小框至少一半被覆盖才可合框，优先交集/最小外接框比例高的组合；重算共享供给，避免重复使用。
6. 以热点紧迫度排批；同批框不争用bin或完整PU，可以并行规划/重排；不同die本地处理可并行。下一批基于实际写回后的需求重新规划，而不是强迫相邻热点沿同一批空隙长成细条。
7. 本die先疏解，确认阶段限定本地面积内无法继续缓解后才进入跨die兜底；框面积上限随阶段，跨die移动预算另有20bin参数，二者不混用。
8. 局部框内使用连续坐标按比例映射，保留相对顺序；本地批次采用100%建议位置，不再混合历史位置，仍保留硬范围/已有位移guard。宏以成员建议位置归并后整体写回，不拆宏。
9. 修正浮点切口与实际落格的口径差异；增加器件空LUT/FF bin元数据，容量0目标不接收需求并投影到邻近有资源处，避免硬编码U250某一列。
10. 阶段扩区预算保留减半开关；当前早期200→100，后期原参数先恢复到50、再减半成25，避免后期继续缩成约12bin。

五、SA与加速
1. SA每个restart使用独立起点、初始代价和最优坐标；最终返回的坐标必须复算出相同最优分。修复线程起点及返回坐标/分数错配，评分公式不改。这不是重写partition算法。
2. CLB候选代价的不可变部分在同一次legalize内复用，候选/列任务分配更均衡；每次legalize退出释放缓存，避免跨阶段长期占用。
3. STA pin坐标缓冲复用，恢复/使用既有静态并行更新分支，不改时序模型。
4. 增量STA跳过空层，1–12节点小层串行，较大层并行。保留PU/节点/边工作量和计算/等待等轻量profiling接口，开销记录与算法结果分开。

六、配套后端（独立于AMF二进制）
1. 普通LUT分组清理提前到首次CARRY整站提交之前，同时保留LUT6_2必要配对。
2. CARRY按整站原生提交，避免分批导入造成临时OUTMUX冲突。
3. SRL按每条完整链独立原生提交一次，保留原LOC/BEL，后续批次引用不重复提交。此项是2026-11-15独立导入脚本变更，不属于17f71c50的C++二进制新增功能。
4. 导入后保留位置锁定做Vivado place，原位保持和复合RAM完整物理占用独立审计。最近一次路由复用placed DCP，没有再次跑AMF/place。

当前复用配置摘要
普通Pro，ProPrimitiveTiming=false；ClockPeriod=3.332ns及502条ClockPeriod覆盖；GlobalPlacementIteration=30、主packing预算30、jobs=12。
sllSTE=true；CoarseFFReserveFirst=true；PackingBoundedRefillReuse=true；SLICEMFFPacking=true；SLICEMSRLLogicPacking=true；SRLMaxPerSLICEM=8；BatchedHotspotSpreading=true。
EarlyOrdinaryAnchorBoost=true；GPOrdinaryAnchorMultiplier=5；OrdinaryAnchorAllGPRounds=true；GPAlwaysLimitQPStep=false。
GPHalfSpreadBinBudget=true；CrossDieSpreadMaxBins=20；SpreadTriggerStats=false。
BoundaryAwareClustering=false、BoundaryRegionClipping=false：关闭新版SLR/HPIO投票/裁侧，保留旧版clock列选择；不等于关闭器件物理边界和SLR延迟模型。
SllRegionPolicy=preserve；release-single是已经实现的实验策略，当前不启用，仍保留在参数清单。
这次30帧仅把上述历史配置的visualization开启；新模型/其他工作区后续修改不自动合入这个冻结版本。

验证范围与现状
AMF完整布局已完成，最后一批加速修改的最终Tcl与父版逐字节一致。随后对同一布局完成Vivado导入、locked place和route；全设计原语LOC/BEL变化0，全部可布线net完成。setup仍未满足（最近布线WNS -3.754ns、TNS -45882.035ns），不能把位置保持和布通当作时序收敛。
候选可放宽、资源利用改善不保证全局CLB单调下降；FF配对和宏粒度会影响partition/SA/GP，多个改动一起的最终QoR不能单变量归因。

## 使用边界

源码按 `17f71c50` 冻结；其他工作区后来的 HPIO 0.15 ns 模型及新试验不自动并入。SLL STE 和扩散等功能由运行配置选择，本次未更改全局默认配置。配套 SRL 导入适配的调用说明见 `scripts/diagnostics/srl_chain_atomic_import/README.md`。
