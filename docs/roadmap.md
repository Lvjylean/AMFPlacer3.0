# 开发路线

| 里程碑 | 交付 | 状态 |
|---|---|---|
| M0 | 版本化项目、可重复构建、全量 benchmark、统一实验记录 | 已有全量基线、新导出 opt DCP 和原生 Vivado 对照；PDF 第一步按当前范围关闭，通用接口健壮性继续完善 |
| M1 | DesignSnapshot、NetlistDiff、CheckpointStore | 待开发 |
| M2 | ImpactAnalyzer、活动/冻结区域、单芯粒局部布局与打包 | 待开发 |
| M3 | 增量控制器、失败回退、Vivado 接口与复用计量 | 待开发 |
| M4 | 多 SLR 拓扑、SLL 代价、跨芯粒资源协调 | 复用独立 partition/floorplan 前端；AMF 接入、器件支持及 SLL 模型待开发 |
| M5 | 对照与消融实验、复现说明、交付版本 | 待开发 |

当前阶段决议：PDF 第一步“网表输入处理与初始分析”按用户确认的范围完成；第二步的 AMF 仓库核查见 [第二步评估](research/step-02-topology-floorplanning-assessment.md)。后续发现用户独立 `non_dataflow_case` 项目已有多 SLR 资源感知 partition 与 floorplan，故优先复用该前端，先验证 cell 群软区域接口，再补目标器件和跨 SLR 模型。详见 [跨项目接入评估](research/partition-floorplan-amf-integration-assessment.md)。本次仅评估，尚未完成 AMF 集成。

增量方向的后续顺序仍为：准备可核对变化的 V0/V1 小设计 → 定义快照和差异格式 → 实现只读差异报告 → 接入局部优化。通用宏导出契约和输入诊断作为工程完善项，不再阻塞从 PDF 第一步进入第二步。多芯粒器件和设计数据应提前准备。上表 M0-M5 是工程里程碑，不与 PDF（1）-（9）编号一一对应。

跨版本复用必须基于稳定对象映射，不能复用旧 cell ID 索引后直接继续布局。每个里程碑达到验收条件后更新需求追踪表，不能以文件或模块名称存在作为实现完成。
