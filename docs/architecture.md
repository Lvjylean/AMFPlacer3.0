# 架构边界

当前代码沿用 `src/lib/HiFPlacer` 下的网表、器件、布局、打包、合法化与时序模块。项目初始化仅增加工程管理入口。

计划数据流：

```text
V1 DCP / 网表 + 旧 V0 状态
  → 规范化快照与对象匹配
  → 变化及影响范围分析
  → 增量控制器
  → 恢复有效位置、冻结资源、局部布局/打包/合法化
  → 时序和资源检查；必要时扩域或全量回退
  → Vivado 实现与复用度量
  → 保存 V1 状态、版本和实验指标
```

拟新增模块为 DesignSnapshot、CheckpointStore、NetlistDiff、ImpactAnalyzer、IncrementalController 和多芯粒拓扑/代价层；以上名称是设计规划，目前没有实现这些新模块。

现有导出脚本会清空布局并调用全局 `place_design`、`route_design`。它适合全量基线，不构成后端增量复用。当前 PU 状态加载依赖 cell ID；必须补充跨版本映射、格式版本及失效校验后才能用于增量设计。
