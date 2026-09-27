# CLB 导入合法性修复

## 状态

2026-09-27：已完成五类错误的原始 DCP 局部复现、首轮 C++ 修复、严格导入关卡及初始打包预检；完整布局、严格导入和布线回归尚待完成。不得把本报告的局部成功解释为零拒绝全设计验收。

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

三个实验共用冻结二进制 `build-20260927-124444-562943-a2a94a48/build/AMFPlacer`，源码修复提交 `1f496daf`。服务器并行运行，耗时不能作为隔离速度测量。

| 模式 | 运行目录（`experiments/runs/` 下） | 范围 |
|---|---|---|
| delay | getrf-u250-full-20260927-125004-730657 | AMF、严格导入、Vivado placement/routing、DRC、时序 |
| control | getrf-u250-full-20260927-125907-199280 | AMF、严格导入，`--import-only` |
| cluster | getrf-u250-full-20260927-125913-760094 | AMF、严格导入，`--import-only` |

仍须等待正式验收，不能将启动运行标记为完成。

架构参考：[AMD UG574 Storage Elements](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Storage-Elements)、[Carry Logic](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Carry-Logic)。上述具体路由限制以本项目 Vivado 2024.2 的定点实验为证据。
