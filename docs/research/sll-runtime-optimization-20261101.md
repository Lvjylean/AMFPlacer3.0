# SLL 运行时等价优化

基于 9f63b97b，保留已有新 L、数量、容量、动态 τ 及刷新调用点。

- TimingOptimizer 仅在 STA / slack 只读批次内缓存 SLL 时序开关；退出（含异常）恢复之前的缓存状态，批次外继续读取当前配置，保留同对象切换开关的行为。其他可变时序系数仍实时读取。
- TimingBatch 只覆盖刷新输入收集、STA 边求值和 slack 阈值扫描。建立共享前缀后并行只读，退出即失效。分别使用 placement pin、STA pin、cell center 坐标；不同源坐标查询回退。refresh/project/model setter 在活动 batch 内拒绝。
- 前缀逐 crossing 保留 `d += delay(cur,in) + slowMax` 的原有浮点顺序。输出段和长尾校正逐接收端原样求值；非树测试输入回退原算法。新计划关键性更新使用函数局部前缀。
- 日志真正打开且可写时才计算纯线长汇总。算法中的 wireCost 不删，索引、virtualBase 和容量重建不跳过。
- 复用收集槽位和前缀向量容量。仍按原 net/pin 顺序收集和排序，不降低刷新频率。
- 新 profile 子分类：collect、rebuild、criticality、index、diagnostics、timing_prefix。与旧大 scope 比较时需要重新汇总其后代，不能将新 self 与旧 inclusive 直接比较。

验证要求：独立模型位级延迟/关键性对照、真实 U250 四寄存器集成、历史重建输出对照，然后单轮同配置 AMF-only，比较最终 Tcl 与规划及 37/92 工作量。新构建耗时单列，未布线不报告新 WNS。

兼容修正：最初 68d1fbf0 候选将开关缓存到构造时，补充审查发现会违反既有 checkFeatureIntegration 的同对象动态切换测试。其固定配置运行仅保留为候选证据；r2 改为只读批次缓存，使用原功能测试验收，不修改测试来接受行为变化。

后续 profile 决策：首候选 1378.647 s 对基线 1354.700 s 没有获得总墙钟加速；slack 阈值扫描仍为 47.899 s。将其独立 net 的只读计算分配到既有线程：每线程整数直方图、每 net 独占计数，join 后按原 net 顺序发布 map。每条延迟及浮点阈值求法保持，异常在 join 后重抛。原功能测试保持，另加 1/12 线程、SLL on/off、正/负/混合 slack 的逐 bin、逐 net 和位级阈值比较。完整 GETRF 还需核对所有 Slack distribution / slackThr 日志。
## 完整 GETRF 验证结果

同一 eda072、10 ns、12 线程、SLL 完整开启、无可视化、CrossDieSpreadMaxBins=0，仅执行 AMF 布局和计划导出。有效算法配置与输入未变。

|范围|原版 / s|优化 / s|
|---|---:|---:|
|AMF 进程墙钟|1354.700|1291.309|
|全局布局阶段合计|807.161|767.443|
|Slack 阈值扫描完整窗口|56.583|11.051|
|STA 完整调用窗口|219.116|205.044|
|规划刷新完整窗口|80.116|76.961|

总计节省 63.391 s（4.68%）。函数窗口可能包含子 scope，不能与阶段窗口相加。单次串行实测不代表稳定均值，收益也不全属于 SLL 特有计算。

37 轮全局布局、92 次 QP、307 次规划刷新、214 次 STA、49 次 slack 扫描均未减少。最终布局 Tcl、资源/级联表、FinalLUTFF 解压内容、SLL 计划字节完全一致。PUInfoFinal 的无序 PULegalization 记录顺序有差异，121165 条记录排序后内容一致，其余解压字节也一致。所选 371 行算法日志（含 slack 分布和阈值）一致。

规划仍为 9446 nets / 9467 crossings，边界 0–1 为 8974、1–2 为 493；最拥挤列 75%，1062 个使用 tile 对，无超容量。未执行 Vivado，不能由此给出新 WNS 或实际布线资源结论。

生产构建来自 daa5b47f0990eaee6470b4e284d3ff698f33a2cb；测试修正提交 6dfbbbd749ca3f6bf7f2e959d060c1e946740c4d 只修复 printf 输出捕获，所有生产文件与构建快照相同。模型位级比较、动态开关原功能测试、1/12 线程并行等价测试及完整布局对照均通过。

证据保存在独立 comparison `experiments/comparisons/getrf-u250-10ns-sll-runtime-optimized-r2-20261101`：`comparison.json`、`paired-evidence.json`、`sll-evidence.json`、`functional-qualification.json` 和中文 `report.md`。大型实验产物不入 Git。
