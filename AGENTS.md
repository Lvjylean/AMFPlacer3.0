# AMFplacer3.0 开发约定

- 项目处于 `3.0.0-dev`：保存经过验证的 AMFPlacer 2.0 基础，并逐步实现课题三的 FPGA 增量编译器。未实现的模块必须明确标注计划状态。
- 正式服务器根目录：`/Projects/jinyang/workspace/AMFplacer3.0`，通过 `ssh eda072` 以 jinyang 使用。
- 使用 `scripts/amf3.py` 构建和运行。构建目录独立，禁止把旧 `build.sh` 的清空逻辑当作默认构建方式。保留用户源码修改和失败实验。
- 新分支默认使用 `codex/` 前缀。记录当前源码提交、工作区变更、输入哈希、工具版本和构建目录。Git 中不存放 DCP、构建产物、运行日志、许可证或大型运行缓存。
- 最终 DCP 留在服务器 `experiments/runs/<run-id>/reports/`；本地同步使用 `scripts/sync_reports.py`，仅传输轻量报告与日志。下载 DCP 需要用户明确提出。
- 所有实验使用唯一 ID，保留 manifest、status、config、日志和报告。运行成功、时序通过、DRC 检查和位置复用率分别记录，不以一个退出码替代全部结论。
- 无变化、参数变化、连接变化、增删单元、约束变化和缓存不兼容应分别测试。保持冻结对象和资源占用的一致性；不得将数组编号相同当作跨版本对象匹配。
- 速度对比须针对同一个 V1 设计与一致工具/参数条件，区分 AMF 阶段加速和端到端加速。
- 本次初始化不实现增量算法。既有未提交源代码修改已作为基线保存，不得误记为本次新算法成果。
- 所有本项目文件放在项目内，遵循 `docs/workspace-layout.md`。Python/Tcl 文件使用 snake_case，新增文档和普通目录使用小写 kebab-case；历史实验 ID 和上游源码命名保持稳定。
- 单次诊断脚本放在 `scripts/diagnostics/`，预检运行放在 `experiments/preflight/`，诊断证据放在 `experiments/evidence/`。旧脚本仅存于 `archives/legacy-workspace/`，不得当作当前入口。
- 时序目标以用户提供给 placer 的约束为准；当前入口采用配置中的 `ClockPeriod` 及可选 `ClockPeriod:<driver>`。网表连接不能独立给出目标周期，DCP 约束提取为可选辅助，不能自动覆盖用户配置。不把“未自动导入 DCP/XDC”单独作为第一步未完成的依据。目标约束、估计延迟及 Vivado 后端约束分别记录。
- 2026-09-25 用户确认 PDF 第一步“网表输入处理与初始分析”按当前支持范围完成。通用外部宏格式、独立分析入口与全面输入诊断保留为工程完善项，不再据此阻塞第二步。第二步仍为部分完成：AMF 已有 2.0 聚类、PaToH 划分和模拟退火基础；用户独立 `non_dataflow_case` 项目已有资源感知划分、多 SLR 区域规划及软 Pblock，但尚未接入 AMF。不得把 AMF 仓库缺口误记为用户未做过对应工作；优先复用该前端。详见 `docs/research/step-02-topology-floorplanning-assessment.md` 和 `docs/research/partition-floorplan-amf-integration-assessment.md`。
- 2026-09-26 用户收缩当前移植范围：只修改 AMF，以 U250 和原始 GETRF post-opt DCP 为目标；暂不接入其 partition/floorplan、membership 或软 Pblock 结果，暂不实现 SLL 优化。该决定覆盖上条在当前阶段“优先复用前端”的实施顺序，独立前端能力仍保留在既有成果中。需要导入真实 U250 器件数据并补充 URAM 支持；跨 SLR 合法性根据专用级联端口连接判断，不能笼统禁止普通分组或独立 RAM/DSP 跨 SLR。范围与输入预检见 `docs/research/u250-getrf-porting-scope.md`。
- U250 第一阶段已实现并验证器件/输入模型，详见 `docs/research/u250-input-adaptation.md`。`configs/experiments/getrf-u250-input.json` 只用于 `amf3.py inspect`；第二阶段已加入独立 URAM 资源分配、硬资源合法化和 Carry/DSP 专用级联 SLR 检查，见 `docs/research/u250-resource-legalization.md`。`amf3.py legalize-resources` 仅执行硬资源阶段，完整布局入口仍有显式保护。原始划分清单省略时钟负载，必须使用从同一 DCP 补齐时钟后的 `data/reference/getrf-u250/amf-inputs/`，不能把旧 `getrf.zip` 当作完整输入。

- GETRF 全流程入口为 `amf3.py full-run`，配置 `getrf-u250-full.json` 显式开启实验性多 SLR。MUX/SRL、CLB 试插入计数、无固定 I/O 初始化和分数列溢出修复已实现；完整布线验收状态以 `docs/research/u250-getrf-full-flow.md` 为准。覆盖检查不完整时不得进入正式后端验收，不能把 Vivado 补放遗漏单元记作 AMF 的完整输出。当前标准配置保留 `GlobalPlacementIteration=30` 和默认宏合法化模式；9 次前期迭代曾导致 QP 发散，`DirectMacroLegalize=true` 曾不收敛，失败轮次保留供诊断。
