# SLR / HPIO 阶段选择（76 独立副本）

本副本继承 75 的阶段选择实现，在 74q 代码上整合 2026-10-11 的两种阶段安排。没有替换为历史整份源码，也没有修改已有实验配置。历史结果继续按其原始源码和二进制解释。

## 参数

`BoundaryStageMode` 使用 AMF 配置文件中的字符串值：

| 值 | 行为 |
| --- | --- |
| `independent-max2`（默认） | 每次建立新的多 SLR 聚类后，先执行最多两轮独立 SLR GP；随后使用原预算执行 HPIO GP。正常提前收敛可以不足两轮。 |
| `merged` | 完成 SLR X 向腾挪、SLR 投影、HPIO Y 向腾挪和 HPIO 投影后，在同一 GP 阶段同时施加 SLR Y 与 HPIO X 牵引；不增加独立 SLR GP 预算。 |

仅当 `BoundaryAwareClustering=true` 且 `BoundaryClusteringStrategy=slr-hierarchical` 或 `cr-slr-hierarchical` 时生效（两个策略均要求 `BoundaryAwareClustering=true`），仍需已有的 `PhysicalBoundaryMode=true` 前提。该参数本身不启用聚类、不增加聚类、不改变投票对象和目标选择规则。

例如，在已有 slr-hierarchical 配置中选择共同作用：

```json
"BoundaryStageMode": "merged"
```

省略参数等价于 `"BoundaryStageMode": "independent-max2"`。74q 的 `BoundaryAwareClustering=false` 配置保持不变，两种阶段均不会因此启动。拼写错误的值会报错。启动脚本对显式阶段选择检查二进制能力，避免旧二进制静默忽略参数。

## 保留的语义

- 初次区域腾挪仍延后到 fixedCLB 初始化完成；之后重建聚类时重新准备阶段。不重建时，不重复区域准备。
- 独立模式进入 HPIO 后关闭 SLR Y 锚点，但保留原 SLR 区域限制；共同模式同时保留 SLR Y 与 HPIO X 锚点。
- `BoundaryWeakAttraction`、`BoundaryAnchorStopOnRegionEntry` 和 `BoundaryRegionClipping` 继续使用 74q 的实现。没有恢复历史代码中把弱牵引与 DSPCritical 耦合的写法。
- 不改变 fixed / locked 判定；不移动已固定或锁定对象，不修改 DFX 硬 Pblock、专用级联、边界留空或区域交集规则。
- 保留静态缓存、FF/LUT/DSP 局部引脚访问约束、LUT LOCK_PINS、同网/常量/专用连接分类和 clock guidance 修补。保留 74q 关闭纵向拉伸的现状。
- 旧诊断环境中的 independent-after-init 阶段预算覆盖仍保留，仅属于旧诊断机制；正常配置不设置该环境变量。

## 验证范围

通过独立构建和 66-cell 合成网表测试核查阶段预算、X/Y 牵引、初始化延后、重复准备保护、固定/锁定对象、默认/显式值等价、关闭聚类时开关无效，以及 Pblock / 增量区域交集与投影开关。复用 74q 时钟批量与 opt-in 单元测试。

完整 GETRF 布局、全量/增量布线、时序和性能实验均未重跑；本次不能用于声称共同作用版改善了 SLL 或时序。没有替换默认构建，没有提交或上传 Git 分支。

结果和来源清单位于本副本上级目录的 `implementation-manifest.json`、`validation-01/manifest.json` 与 `preservation-audit.json`。


78 新增 CR 边缘锚点分支，阶段预算及调度不变；锚点区别见 [cr-slr-hierarchical.md](cr-slr-hierarchical.md)。
