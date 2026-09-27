# CLB 导入合法性修复

## 状态

2026-09-27：五类错误的原始 DCP 局部复现、C++ 修复和预检已完成。delay、cluster 已通过全设计严格导入；delay 的 Vivado placement 也保持全部 AMF 位置。delay 布线尚在运行。control 首轮暴露原时钟区域列搜索停滞，已补充修复和原生测试，正在独立目录重跑。完整回归尚未全部验收。

服务器根目录 `/Projects/jinyang/workspace/AMFplacer3.0`，分支 `codex/clb-import-legality`。起点提交 `a2a94a48aeed6a4e56c572441574b07c586121ef`。最终 DCP 仅保留在服务器。

## 固定基线

delay 运行 `experiments/runs/getrf-u250-full-20260927-063459-913333`：输出 856998 个 AMF 单元，导入后 1782 个尚未放置；后续 Vivado 修复并完成布线。因此完整流程成功与 AMF 输出本身合法分别计量。

输入 `data/reference/getrf-u250/post_opt.dcp`，SHA256 `6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`。Vivado 2024.2。10 ns 目标不变，没有添加 OOC 时钟来源或 SLL 模型。

## 复现与根因

`scripts/diagnostics/extract_import_conflicts.py` 按失败 cell 与请求位置关联 site。基线日志指向 318 个 site：OUTMUXC 256、FFMUXA1 39、SET/RESET 21、LUT 输入数 1、SRL shape 1；类别计数是 site 计数，日志事件与未放置 cell 数另行记录。

`experiments/preflight/clb-import-replay-20260927-02` 保留完整网表下 11 个失败 site、3 个正常 site 的原顺序、逆序、逐 cell 测试。所有失败类型均复现，正常对照全部通过。大批次报错所打印的最后一个 cell 并不总是冲突的直接来源。

`clb-import-replay-20260927-03` 使用等价的非空 HLUTNM/SOFT_HLUTNM 属性清除，结果与 02 一致。实际非空数量为 0 / 116359，避免向所有层级单元反复设置空属性。旧方法的长耗时发生在预处理，不是 DCP 读取本身；02 的 open_checkpoint 用时约 76 秒。

`clb-import-replay-20260927-04/repair_probes.tsv` 验证具体修复：

- Carry O/CO 同位输出需要 fabric 通路时，样本半区进入特殊 route-through 配置，八个 FF BEL 均不能放置顺序逻辑。删除一个 CFF 无效；移出整个半区的 FF，三个样本均通过；单 FF 放置在 A–D 各槽失败，E–H 各槽通过。初始 Carry 宏现在不吸收受影响半区的 FF，并通过虚拟槽位保留整个半区。专用 CO[7]→CI 级联不算 fabric 输出。
- 动态 CI 使用 AX，与无关 AFF 的旁路输入冲突。三个样本移除 AFF 后均通过，两个移动到空 HFF 后通过；另一个 HFF 控制集不兼容，不能盲目搬移。初始宏为动态 CI/CI_TOP 保留对应主 FF 槽。
- SET/RESET：最终槽位映射中将 FF 移入空控制组时，只检查了本组，漏查另一组共享的 CLK/SR。Carry、普通和 MUX 三条映射路径统一检查实际槽位内容。
- LUT 输入数：详细布局后插入 LUT/FF 对时漏查伙伴 LUT 的共享输入；现复用同一选择/提交函数，检查 LUT6 独占与共享输入多重集。
- SRL：原来的单独 SLICEM 分配拆开了三单元 Q31 链，与 Vivado shape 限制冲突。连续 C/B/A、F/E/D、H/G/F 三种整体放置均通过。未被其他宏拥有的线性 SRL 链现在构造统一 MCLB 宏；当前实现支持同 CLK/CE、至多八单元的单 SLICEM 链，其他不支持结构明确报错。

原生旧布局审计覆盖全部 21 个 SET/RESET site 与唯一 LUT 输入错误 site。检查器还报告了保守控制集兼容规则限制的其他组合，不能把原生告警总数等同于 Vivado 拒绝数。

## 验收流程

`CLBSiteLegality.h` 复用 LUT 配对、FF 半区/控制组合法性；最终 Tcl 导出前再检查控制集和 LUT 组合。内部通路通过初始宏的资源预留实现，Vivado 仍是最终导入判据。

`full-run` 默认 strict：导入审计之后、`place_design` 之前，必须请求单元全部存在、全部放置、原 LOC/BEL 完全一致、拒绝事件为零、SRL/硬资源级联违规为零。失败保留差异表并退出。`--import-only` 只运行该关卡；`--allow-import-repair` 仅供显式历史诊断，并单独标记，不能冒充严格验收。

`reports/import_acceptance.tsv` 与阶段位置报告一起保存；完整汇总单独给出 `strict_import_verified`、DRC、布线完成、时序与最终位置保持率。即使严格导入通过，仍保留 Vivado `place_design`，是否可去掉属于另一项验证。

## 首轮验证

- 61 项 Python 测试通过；`experiments/preflight/clb-legality-python-20260927-01.log`。
- C++ 控制组/共享输入/实际槽位提交正反例通过；`clb-legality-unit-20260927-02.log`。
- 6 项 MUX/SRL 原生回归通过；`clb-legality-mux-20260927-02/results.json`。预期失败的控制不兼容和重复归属案例保持失败。
- 完整 GETRF 初始打包通过，7983 项 MUX trial/copy/remove/reinsert 检查通过；`experiments/runs/getrf-u250-packing-20260927-124630-816660`。
- 冻结构建 `builds/build-20260927-124444-562943-a2a94a48` 包含当时工作树 C++ 修复，来源通过其源代码快照及 source_hashes.json 记录，不能只用基线提交号代指修复代码。
- Carry 初始宏的三个原生正反例通过：特殊 route-through 半区预留、动态 CI 预留、专用 CO[7]→CI 保留正常 FF 打包。结果 `experiments/preflight/clb-initial-native-20260927-04/results.json`；追加测试构建 `build-20260927-125402-193404-1f496daf` 与全流程构建的生产源码哈希完全一致。
- `carry_rule_coverage.json` 对照全部旧 Carry 报错：OUTMUXC 256/256、FFMUXA1 39/39 被新规则解释，无未解释的同类 site。此结论不是新布局零错误的替代证据。
- 不可变首轮证据：`experiments/evidence/clb-import-legality-20260927/preflight-01.json`，含输入、二进制、脚本、测试与报告哈希，以及失败诊断夹具的记录。

## 进行中的完整回归

首轮三个实验共用冻结二进制 `build-20260927-124444-562943-a2a94a48/build/AMFPlacer`，源码修复提交 `1f496daf`。服务器并行运行，耗时不能作为隔离速度测量。

| 模式 | 运行目录（`experiments/runs/` 下） | 范围及当前结果 |
|---|---|---|
| delay | getrf-u250-full-20260927-125004-730657 | 严格导入通过，placement 后位置仍全部保持；routing、最终 DRC/时序待完成 |
| control 首轮 | getrf-u250-full-20260927-125907-199280 | AMF 候选搜索达到上限，256 个 PU 未打包，失败记录保留 |
| cluster | getrf-u250-full-20260927-125913-760094 | `--import-only` 完成，严格导入通过 |
| control 补充 | getrf-u250-full-20260927-144536-634632 | 使用补充修复构建重跑 AMF、严格导入；待完成 |

delay 严格导入：856998/856998 个单元存在、已放置、与原 LOC/BEL 完全一致；拒绝事件 0。12183 条硬资源级联和 325 条 SRL 级联违规均为 0。Vivado `place_design` 后上述结果保持，日志报告所有实例均已放置。

delay 当前计时：AMF 4114.210 秒，DCP 读取 87.269 秒，导入 737.461 秒，导入审计 17.939 秒，placement 218.852 秒，placement 审计 20.711 秒。全流程总耗时和最终时序须等待 routing 与报告生成。导入日志仍有 MUX shape 的 INFO 提示，但没有产生拒绝或位置差异。OOC 端口位置和局部 SLL 需求警告在旧基线中也存在；本轮不补造端口/时钟约束，不引入 SLL 容量模型。

## control 补充修复

首轮 control 在重新分配阶段始终剩余 256 个 PU，扩大半径至 300 后触发失败。终止输出与实际器件库关联后，全部单元都属于 X 时钟区域第 3 列。原搜索即使扩大半径，仍只接受同列候选。现有日志并不能单独证明该列总资源耗尽；需要解决的是原列无法满足打包时，搜索不能尝试相邻列的缺口。

`ParallelCLBPacker::exceptionHandling` 现在在连续八轮无进展后解除内部时钟列筛选，并将搜索半径重置到近邻范围；保留实际 BEL、控制组、宏和硬资源约束。两种候选查询都排除未纳入当前 packer 的保留 site。最终搜索失败改为明确异常，避免在关闭断言的构建中继续输出不完整结果。停滞诊断适用于所有模式。

`checkBoundaryPacking --legacy-stall` 使用真实 U250 时钟列，将原列全部 Slice 标记为不可用，只在相邻列保留一个合法 site，验证完整停滞回退实际将中间 LUT 放至该 site。原有四种查询测试与物理模式跨列合法化也通过。证据：`experiments/preflight/clb-column-fallback-20260927-01`；失败单元和时钟列关联记录在 `experiments/evidence/clb-import-legality-20260927/control-stall-02.json`、`control-stall-regions-01.json`。

补充提交 `11b37db4`，冻结构建 `build-20260927-144247-568663-8c570531`。该构建与首轮构建仅有一个生产源码文件差异：`ParallelCLBPacker.cc` 的候选搜索与失败处理。delay 和 cluster 首轮重新分配的剩余数量逐轮减少，均没有连续停滞，因此新增回退条件在这两条已完成的 AMF 执行轨迹上不会触发。两个构建的验证范围分别记录，不能将首轮布线结果冒称为新构建的完整重跑。不可变补充证据：`experiments/evidence/clb-import-legality-20260927/fallback-01.json`。

架构参考：[AMD UG574 Storage Elements](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Storage-Elements)、[Carry Logic](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Carry-Logic)。上述具体路由限制以本项目 Vivado 2024.2 的定点实验为证据。
