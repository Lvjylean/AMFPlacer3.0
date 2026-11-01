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
