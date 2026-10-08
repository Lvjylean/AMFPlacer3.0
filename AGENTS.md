# AMFplacer3.0 开发约定

- 项目处于 `3.0.0-dev`：保存经过验证的 AMFPlacer 2.0 基础，并逐步实现课题三的 FPGA 增量编译器。未实现的模块必须明确标注计划状态。
- 正式服务器根目录：`/Projects/jinyang/workspace/AMFplacer3.0`，通过 `ssh eda072` 以 jinyang 使用。
- 使用 `scripts/amf3.py` 构建和运行。构建目录独立，禁止把旧 `build.sh` 的清空逻辑当作默认构建方式。保留用户源码修改和失败实验。
- 新分支默认使用 `codex/` 前缀。记录当前源码提交、工作区变更、输入哈希、工具版本和构建目录。Git 中不存放 DCP、构建产物、运行日志、许可证或大型运行缓存。
- 最终 DCP 留在服务器 `experiments/runs/<run-id>/reports/`；本地同步使用 `scripts/sync_reports.py`，仅传输轻量报告与日志。下载 DCP 需要用户明确提出。
- 所有实验使用唯一 ID，保留 manifest、status、config、日志和报告。运行成功、时序通过、DRC 检查和位置复用率分别记录，不以一个退出码替代全部结论。
- 2026-09-30 用户明确要求：AMF3 常规 `full-run` 沿用原版允许 Vivado 后端修复的策略，默认 `import_policy=repair`，直接进入修复及布线，不再要求先做严格导入失败轮次。`--strict-import` 和 `--import-only` 保留为可选严格诊断；`--allow-import-repair` 保留兼容。两种模式都记录导入差异、最终位置保留率、完整路由、DRC 与时序，修复结果不改报为严格导入通过。完整导出覆盖和实际约束检查仍保留；历史实验与源码快照不改写。该决定覆盖旧文档中“full-run 默认 strict／repair 仅作诊断”的常规流程约定。
- 无变化、参数变化、连接变化、增删单元、约束变化和缓存不兼容应分别测试。保持冻结对象和资源占用的一致性；不得将数组编号相同当作跨版本对象匹配。
- 速度对比须针对同一个 V1 设计与一致工具/参数条件，区分 AMF 阶段加速和端到端加速。
- 2026-09-28 用户明确要求：AMF 实际布局运行与 Vivado 文件格式适配分别统计，适配时间不计入 placement。DCP→AMF 导出、AMF→Vivado 导出/转换/导入单列；Vivado place_design、route_design、审计/报告/DCP 写出也各自记录。保留 AMF 进程墙钟与端到端总墙钟用于核对，不把进程总时间称为纯算法时间。历史未完整分段的部分标记未单独计时；复用输入缓存说明本轮未执行导出，不伪造首次导出成本。详见 docs/experiment-policy.md。
- 本次初始化不实现增量算法。既有未提交源代码修改已作为基线保存，不得误记为本次新算法成果。
- 所有本项目文件放在项目内，遵循 `docs/workspace-layout.md`。Python/Tcl 文件使用 snake_case，新增文档和普通目录使用小写 kebab-case；历史实验 ID 和上游源码命名保持稳定。
- 单次诊断脚本放在 `scripts/diagnostics/`，预检运行放在 `experiments/preflight/`，诊断证据放在 `experiments/evidence/`。旧脚本仅存于 `archives/legacy-workspace/`，不得当作当前入口。
- 时序目标以用户提供给 placer 的约束为准；当前入口采用配置中的 `ClockPeriod` 及可选 `ClockPeriod:<driver>`。网表连接不能独立给出目标周期，DCP 约束提取为可选辅助，不能自动覆盖用户配置。不把“未自动导入 DCP/XDC”单独作为第一步未完成的依据。目标约束、估计延迟及 Vivado 后端约束分别记录。
- 2026-09-25 用户确认 PDF 第一步“网表输入处理与初始分析”按当前支持范围完成。通用外部宏格式、独立分析入口与全面输入诊断保留为工程完善项，不再据此阻塞第二步。第二步仍为部分完成：AMF 已有 2.0 聚类、PaToH 划分和模拟退火基础；用户独立 `non_dataflow_case` 项目已有资源感知划分、多 SLR 区域规划及软 Pblock，但尚未接入 AMF。不得把 AMF 仓库缺口误记为用户未做过对应工作；优先复用该前端。详见 `docs/research/step-02-topology-floorplanning-assessment.md` 和 `docs/research/partition-floorplan-amf-integration-assessment.md`。
- 2026-09-26 用户收缩当前移植范围：只修改 AMF，以 U250 和原始 GETRF post-opt DCP 为目标；暂不接入其 partition/floorplan、membership 或软 Pblock 结果，暂不实现 SLL 优化。该决定覆盖上条在当前阶段“优先复用前端”的实施顺序，独立前端能力仍保留在既有成果中。需要导入真实 U250 器件数据并补充 URAM 支持；跨 SLR 合法性根据专用级联端口连接判断，不能笼统禁止普通分组或独立 RAM/DSP 跨 SLR。范围与输入预检见 `docs/research/u250-getrf-porting-scope.md`。
- U250 第一阶段已实现并验证器件/输入模型，详见 `docs/research/u250-input-adaptation.md`。`configs/experiments/getrf-u250-input.json` 只用于 `amf3.py inspect`；第二阶段已加入独立 URAM 资源分配、硬资源合法化和 Carry/DSP 专用级联 SLR 检查，见 `docs/research/u250-resource-legalization.md`。`amf3.py legalize-resources` 仅执行硬资源阶段，完整布局入口仍有显式保护。原始划分清单省略时钟负载，必须使用从同一 DCP 补齐时钟后的 `data/reference/getrf-u250/amf-inputs/`，不能把旧 `getrf.zip` 当作完整输入。

- GETRF 全流程入口为 `amf3.py full-run`，配置 `getrf-u250-full.json` 显式开启实验性多 SLR。MUX/SRL、CLB 试插入计数、无固定 I/O 初始化和分数列溢出修复已实现；完整基线 `getrf-u250-full-20260926-183758-400040` 已通过全量布线、DRC 错误/严重警告为 0 和级联审计，10 ns setup 时序未收敛；指标与限制见 `docs/research/u250-getrf-full-flow.md`。稳定构建入口为 `builds/validated-getrf-u250-full/AMFPlacer`。覆盖检查不完整时不得进入正式后端验收，不能把 Vivado 补放遗漏单元记作 AMF 的完整输出。当前标准配置保留 `GlobalPlacementIteration=30` 和默认宏合法化模式；9 次前期迭代曾导致 QP 发散，`DirectMacroLegalize=true` 曾不收敛，失败轮次保留供诊断。

- 2026-09-27 用户要求为现有延迟修正补充纵向 SLR 边界惩罚，覆盖上一阶段“暂不实现跨界时序代价”的限制。参数 `SLRBoundaryDelayNs` 默认 1.5 ns/道边界，GETRF 全流程配置显式设置为 1.5；设为 0 可复现旧延迟模型。原 X 方向修正保留，普通时钟区域的 Y 边界不增加惩罚；SLR 边界从器件元数据推导。仍不加入 SLL 容量或估计拥塞、不接入外部 floorplan。该系数是启发式初值，不代表已校准或新一轮 GETRF 布线后的时序结论。实现、测试与构建见 `docs/research/u250-slr-timing-penalty.md`。

- 2026-09-27 用户批准物理边界 A–D 方案。新模式从 Vivado 全器件 site/tile 与统一坐标映射生成模型；U250 已识别三条 SLR 接缝和内部 HPIO 带，形成 8 个区域。`PhysicalBoundaryMode=true` 替换固定 X 列经验项；`BoundaryAwareClustering=true` 要求新模式，并使用容量预留和 X/Y 软目标。局部硬 IP/未知类型只报告，不把包围盒视为必经障碍。架构规则和系数仍为启发式，不包含 SLL 容量/拥塞。新模式完整 GETRF 对照运行中，不能宣称优于旧模式或替换默认验证基线；实施、构建、测试、比较 ID 见 `docs/research/device-physical-boundary-implementation.md`。

- 上条 A–D 已完成完整验收，最终状态以 `docs/research/device-physical-boundary-getrf-validation.md` 为准。三组均全量布通、DRC/级联合法；本例仅延迟模式 WNS +0.003 ns，二维聚拢模式 −0.059 ns，control −0.137 ns。优先显式使用 `getrf-u250-physical-delay.json` 配合冻结构建 `build-20260927-063221-904893-7a7b5d8b`，保留其中共同数值保护；二维聚拢继续实验，不自动修改全局默认或旧验证构建。OOC 时钟来源警告仍存在，不能将本例 3 ps 余量扩大解释为跨设计或板级稳健收敛。新运行与最终 DCP 均按服务器独立实验目录管理；三组比较和最终哈希证据见 `experiments/evidence/device-physical-getrf-20260927/manifest.json`。

- 2026-09-28 完成 U250/GETRF 的一轮 SA 横纵距离比例数据标定：新配置 `getrf-u250-sa-calibrated.json` 显式使用 `Simulated Annealing y2xRatio=0.71`（有效值，不再乘 0.8），共享 `y2xRatio=0.4`。省略新键时沿用原 SA 计算。正式构建 `build-20260928-002048-349585-d534237b`，报告见 `docs/research/u250-sa-ratio-calibration.md` 和运行 `u250-sa-ratio-calibration-20260928-005532`。1,923 条 routed 连接、保留数据与另一布局验证支持优于旧 0.32 的粗粒度线性代理；不同布局重拟合约 0.55–0.72，不能当作全器件物理常数。本轮未重跑完整 GETRF 布局布线，旧配置仍作对照，不宣称最终 WNS 改善。

- 2026-09-28 随后完成 0.71 的 10 ns 完整实验 `getrf-u250-full-20260928-014049-967020`。与历史严格基线 `getrf-u250-full-20260927-125004-730657`（有效 0.32）相比，总墙钟 147.89→123.88 分钟，AMF 68.57→51.16 分钟，但 WNS +0.111→−0.353 ns、TNS 0→−2.896 ns，26 个 setup 端点违例；hold、全量路由、DRC、级联和 100% 原始 LOC/BEL 保留均通过。最差路径在 SLR 1/2 间跨界五次。0.71 继续作为实验值，不将距离拟合改善表述为端到端 QoR 改善，保留历史 0.32 时序基线。两次构建还存在既有 CLB 打包稳健性差异（本轮未触发列兜底），且服务器负载未严格控制，因此不是纯单变量因果实验。最终 DCP 留在服务器，报告见 `docs/research/u250-sa-ratio-10ns-validation.md`。

- 2026-09-29 用户结束新匹配求解器加速实验，常规使用 `BipartiteMatchingBackend=legacy`；保留 `MacroCandidateSelection=cached_topk` 和只读列表引用优化。五处源码缺省值及常规 GETRF 配置已恢复 legacy。新内核、forward-bias、fast 配置仅供明确选择的历史复现，不自动重新启用。旧内核本身保留原正向 +0.01／反向不抵消；不要再给 legacy 配置新内核的非零 forward-bias。已验证组合及启动命令见 `docs/research/placement-acceleration-inventory.md`，稳定配置为 `configs/experiments/getrf-u250-placement-stable.json`。

- 2026-10-08 发布范围更新：本分支已接入 initialization-only 外部 floorplan，并包含单向分阶段 SLR/HPIO 聚拢与时钟容量表修复；覆盖前文历史阶段的“暂不接入 floorplan”限制。外部区域不永久锁定，未实现 SLL 容量优化和完整时钟布线分配。详见 docs/research/version-diff-20261008.md。历史报告按原日期理解，不改写历史结果。
