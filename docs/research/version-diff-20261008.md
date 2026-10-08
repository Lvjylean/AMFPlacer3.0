# 2026-10-08 分支功能核对与发布范围

本分支基于 2026-10-03 提交 5ea1f494ce90ec71d0bcec10c06bf5e176316fd2，保留 U250 新坐标、新延迟系数、布局修复和 Vivado 接口更新。当前实现取自独立实验副本提交 7235c2ff22b3f844b1e2a40798a56d9a961cf168，并纳入其配套诊断、实验配置和正式目录的两份时钟容量说明。正式源码与原实验副本均不改写。

## 核对结论

| 用户要求 | 当前实现 | 入口或实现 |
| --- | --- | --- |
| 接入外部 floorplan | 保留 InitialPacker、宏及相对偏移；外部 membership 与 CR 集合替代内部粗聚类/SA 初始化。仅提供初始位置，后续全局布局、打包与合法化可移动实例 | scripts/import_external_floorplan.py、ExternalFloorplan.cc、ClusterPlacer.cc |
| 二维聚拢修复 | 真实实例严格多数先选 SLR，再在选中 SLR 内选 HPIO 侧。按原版强度作单向拉动，越过中心后不反向拉回；先 SLR/Y 引导并横向腾空间，再 HPIO/X 引导并纵向腾空间，后者限制在该 die 内。扩散后将已选择 PU 的越界位置裁回 | BoundaryAwareClusterer_Paper.cc、HierarchicalBoundaryPolicy.h、GeneralSpreader.cc、GlobalPlacer.cc、WirelengthOptimizer.cc |
| 时钟容量修复 | 引入器件容量表、part/器件输入哈希校验、半列拓扑和 C++ 表驱动检查，避免对所有器件固定使用同一容量 | ClockResourceCapacity.cc/.h、scripts/clock_resource_capacity.py、configs/architecture/clock-resource-rules.json |

三类功能与本次要求一致。二维策略应选择 BoundaryClusteringStrategy=paper-hierarchical 的最新实现，旧实验配置保留用于历史复现。

## 行为边界

- floorplan 是 initialization-only；没有把外部 CR 集合变成永久软惩罚或硬区域锁定，不能承诺最终完全遵守原面积。
- 二维策略无 SLR 严格多数时跳过；HPIO 平票时只作 Y 引导，X 保持自由。侧别属于本轮边界聚拢/扩散保护，不是永久分区锁。
- 保留原版吸引强度、3/6 距离分段、1.1 次幂及现有配置；本次发布不调整 U250 近/中/远延迟、边界惩罚、STA 或 0.5 ns 候选交换筛选。095 继续沿用既有参数/路径。
- 器件查询已验证 U250 SLICE 半列的容量为 16；这是启用查询并生成对应表时的结果。无查询/无容量表的兼容路径仍保留原值 12；不能理解为全局将 12 改为 16。
- CR track/BBox 容量与 half-column 容量是不同量；当前不是完整时钟布线分配器，也不覆盖所有硬资源的时钟拓扑。
- 资源容量预留不等于 SLL 连线容量。本分支没有新增 SLL 容量优化，不能保证跨 die 连接一定可布通。
- 同时保留必要工程修复：细布局临时对象释放、显式 Vivado 可执行文件与版本记录、目标时钟重定向、串行实验工具，以及可选的全局布局后停止诊断。
- configs/machines/eda072.json 的默认 Vivado 路径为服务器的 2026.1 安装。其他主机必须显式配置可用路径。
- 本次只发布源代码、脚本、配置和轻量说明。DCP、器件缓存、外部 membership 数据、二进制和运行日志不入 Git。示例配置引用既有服务器数据；新环境须导出/导入对应输入，不能直接假定绝对路径可用。
- 本次仅合入上述独立副本和时钟功能；正式目录中并行进行的其他 benchmark/实验改动不包含在本次分支。

## 验证与来源

2026-10-08 在独立发布目录执行：

1. python3 -m unittest discover -s tests -p 'test_*.py'：169 项通过。
2. 编译并运行 src/tests/check_hierarchical_boundary_policy.cc：多数投票、扩展规则、100 组原公式权重和跨中心检查通过。
3. 核对 85 个取自已有工作区的发布文件 SHA-256 与来源一致；另更新本说明、README 和 AGENTS。
4. 保留 1003 仓库中未包含在精简实验副本里的历史文件，不把副本遗漏当作删除。

本次发布不新增完整 GETRF 运行，也不以这次单元回归替代完整布局布线验证。既有工程与实验细节见：
- [单向分阶段边界修复](u250-one-sided-staged-boundaries.md)
- [外部 floorplan 验收](getrf-external-floorplan-10ns-vivado2026-1-validation.md)
- [时钟容量实现](device-clock-capacity-implementation.md)
- [U250 半列容量核验](u250-half-column-device-validation.md)

来源提交包括外部初始化 bbfbd584f5991928d4eea98f9c7073cfcadc22df、对象生命周期修复 08928ffa962162dc0142b3ad3da5c5efcee992c6 和当前副本 7235c2ff22b3f844b1e2a40798a56d9a961cf168。完整发布文件哈希和验证日志保存在服务器实验副本的 experiments/evidence/20261008-github-release/。
