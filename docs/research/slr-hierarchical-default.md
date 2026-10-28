# SLR hierarchical 策略名称与默认值（76 独立副本）

本副本以 75 阶段选择版为父本，保留 74q 增量修补。

| 配置 | 当前行为 |
| --- | --- |
| 省略 BoundaryClusteringStrategy | 默认 slr-hierarchical |
| slr-hierarchical | 规范名称，原 paper-hierarchical 算法 |
| paper-hierarchical | 兼容旧配置的别名，不是另一种算法 |
| region-gain | 仅显式选择；遗留路径，不再是缺省值 |

BoundaryAwareClustering 仍须为 true 才会启用聚类，PhysicalBoundaryMode=true 仍是前提。关闭聚类时，即使省略策略或带有层次策略字段，也不启用边界聚类。配置拼写错误仍报错。

BoundaryStageMode 的默认值仍是 independent-max2，另可选择 merged。阶段预算、投票、锚点、BoundaryWeakAttraction、BoundaryAnchorStopOnRegionEntry 和 BoundaryRegionClipping 的算法均未改动。关闭纵向拉伸、DFX Pblock、静态缓存及引脚访问过滤、LOCK_PINS、clock guidance 等实现保持父本原样。

Python 启动检查与 C++ 使用相同缺省规则。新二进制 capabilities 声明 boundary_clustering_strategy_schema=1 和 boundary_clustering_strategy_default=slr-hierarchical。新名称或省略策略时，启动器拒绝仍采用旧默认值的二进制；显式旧别名仍可按原能力契约运行旧二进制。

本副本的配置示例将旧名称更新为规范名称，三个当前 DFX/full 模板也改为规范名称；历史 validated 配置目录、已冻结部署和旧运行记录不改写。内部 paper* 函数、文件名及历史 schema 字段保留兼容，不表示另有一条 paper 分支。

本次只构建并运行小型配置/合成网表验证，不运行全量或增量验收，不提交、不推送，也不修改桌面 PPT。


78 补充：新增可显式选择的 `cr-slr-hierarchical` 同级策略，不改变本页的默认值和旧别名。详见 [CR 边缘锚点](cr-slr-hierarchical.md)。
