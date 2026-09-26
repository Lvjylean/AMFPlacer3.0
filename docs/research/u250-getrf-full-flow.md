# U250 GETRF 完整布局布线适配

状态：修复版 AMF 完整布局与 100% 单元导出已通过；Vivado 完整布线仍在验证，本记录暂不表示布线成功。

## 范围与输入

沿用原始 `data/reference/getrf-u250/post_opt.dcp`（SHA256 `6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`）、完整时钟连接网表和 U250 器件库。源代码基点为 `fbebbe04`，工作分支 `codex/u250-uram`。本阶段不接入外部 partition/floorplan，也不优化 SLL。

本轮配置显式选择 AMF `ClockPeriod=10` ns；Vivado 保留原始 DCP 中的 `ap_clk=10` ns。二者分别记录，没有从网表推断目标时序。最终 DCP 只保存在服务器。

## 已定位的问题与修复

1. GETRF 的 396 个 MUXF7 输入由 SRLC32E 驱动，而旧代码断言输入只能是普通 LUT。新增 198 个独占 SLICEM 的 SRL/MUX 宏；I0→B6LUT、I1→A6LUT、MUX→F7MUX_AB；保留 SRL 原语类型，检查共享 CLK/CE 和唯一归属。暂不在同一 SLICEM 内混合其他无关逻辑，后续可优化利用率。
2. 旧 CLB 导出会跳过含有三个固定 BEL 的奇数单元宏。现在输出全部固定 BEL 映射，不再用单元数奇偶作为省略条件。
3. 旧资源分类将 SRL 与 LUTRAM 分开，但 SLICEM 合法化只统计 LUTRAM。将原始 SRL 类型纳入 SLICEM 宏资源计数、独立 SRL 打包和导出，仍保留其逻辑类型。
4. 无固定 I/O 的 OOC 输入会使聚类初始化一直等待连接到固定单元的种子。增加无固定种子时的确定性初始簇，随后继续原有贪心初始化和退火。修正最后一批退火线程的多创建一个任务问题。
5. U250 启用实际选中时钟列的吸引目标，避免沿用 VCU108 围绕列 2 和列 1/3 的特殊策略。
6. `MacroLegalizer::spreadMacros` 的浮点预算超额转为整数时可能变为 0，导致列溢出循环不移动任何宏。改为向上取整且至少移动一行；按实际左移的宏行数计算剩余右移数量。GETRF 实际触发值为使用 365 行、预算 364.8 行。
7. 完整流程显式使用 `experimental multi-SLR placement=true`，默认保护仍保留。完整打包后执行硬资源合法性验证，并导出专用级联对，供 Vivado 导入和布线后复核。

8. GETRF 有 325 条 `Q31 → D` 连接：198 条在 B6LUT→A6LUT 的同一 SRL/MUX 宏内，125 条由已有宏的 A6LUT 引出，另有 2 个独立 SRL 源。独立 Q31 源新增固定 A6LUT 导出信息；其宏归属、占用、权重与站点坐标不变。旧运行输出可由适配层将独占 SLICEM 的 H6LUT 纠正到同站点 A6LUT，记录每条变更，并分别统计原始 BEL 匹配、纠正后匹配和站点匹配。后台还将复核 325 条实际 Q31 路径。物理规则依据：[AMD UG574 的 SRL 级联说明](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Shift-Registers-SLICEM-Only)。

9. `PackingCLBCluster::tryAddPU()` 在成功试插入后仅复制 LUT/FF 与 PU 集合，漏掉 `numMuxes`。后续把含 MUX 的簇当作普通 LUT/FF 簇映射，造成 MUX BEL 缺失及专用输入 LUT 顺序错误。现在同步 MUX 计数并使缓存哈希失效；`--inspect-packing` 还检查普通 MUX 宏经过试插入、复制、移除、重新插入和拒绝重复插入后的计数。不是靠补写缺失位置绕过打包器。

SRL32 使用 SLICEM 的依据：[AMD UG574](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/SLICEM-SRL-Shift-Register-Primitive)。真实 BEL 映射另外用同一 GETRF DCP 的 Vivado 实际放置验证。

## 实验与验证证据

- `getrf-u250-full-20260926-150155-081308`：定位无固定单元时聚类初始化死循环，终止该实验并保留状态。
- `getrf-u250-full-20260926-150648-975751`：越过初始打包/聚类，421.740 s 后在密度网格断言退出。补齐 SRL 分类，网格索引采用双精度中间量，并增加单元/坐标/边界诊断以定位后续问题。
- `getrf-u250-packing-20260926-151115-924561`：导出 198 个 SRL/MUX 宏，共 594 个真实单元。
- `getrf-u250-srl-audit-20260926-151231-308935`：Vivado 2024.2 成功放置全部 198 个宏，594/594 LOC/BEL 精确匹配。这是局部物理打包验证，不是完整布局或布线结果。
- `mux-tests-20260926-1519`：4 项真实 C++ 初始打包回归通过，检查正常 SRL/MUX、不同 CLK/CE、输入重复归属、普通 LUT/MUX；正常 SRL/MUX 还要求宏的 SLICEM 资源标志为真。
- `input-regression-20260926-1520`：6 项输入兼容检查通过。
- `resource-regression-20260926-1520`：15 项硬资源检查通过，含 URAM、Carry/DSP 跨 SLR 边界和旧 VCU108。
- `check_sa_floating.cc`：3 个簇、无固定单元、非整除线程批次，在 15 秒超时内完成；每个簇恰好出现一次，坐标均有效。
- `make check`：35 项 Python 检查通过，覆盖输出路径、后端重试、重复赋值、失败批次的带索引单元名以及布线完整性判定。
- `resource-regression-20260926-1700`：新构建 16 项硬资源检查通过，新增两个各 11 站点列的 9.9 行预算回归；新构建约 8 秒完成，旧构建在同一输入上超过 20 秒未结束（主动超时），见 `overflow-before-fix-20260926-1702`。
- `mux-tests-20260926-1843`：最终映射修复构建的 6 项真实 C++ 检查通过，含普通 MUXF7、MUXF8 宏的试插入状态回归。
- `mux-tests-20260926-1816`：出口修复构建 `build-20260926-181230-598338-fbebbe04` 的 5 项真实 C++ 打包检查通过，新增独立 Q31 源必须导出 A6LUT 的用例。
- `mux-tests-20260926-1700`：新构建的 4 项 MUX/SRL 检查通过；`check_column_overflow.cc` 覆盖 GETRF 的 365/364.8 取整边界及非整除预算。

- `getrf-u250-full-20260926-151909-521419`：完成全部全局布局、CLB 打包及细化，未放置 PU 已归零；在 3074.372 s 写出首份打包档案时失败。原因是新入口给 `DumpCLBPacking` 传入绝对路径，而上游 JSON 解析器再次加上 `dumpDirectory`。已改为相对文件前缀，并补充回归检查。此轮没有有效的完整放置 Tcl 或最终 DCP，不计作成功。
- `getrf-u250-full-20260926-161904-089708`：试用上游 `DirectMacroLegalize=true`，硬资源位移反复在约 45～60 单位间波动，928.615 s 主动终止；保留失败状态和终止原因。正式全量配置恢复默认合法化方式。
- `getrf-u250-full-20260926-163444-325508`：使用显式验证配置 `GlobalPlacementIteration=9`；全部前期布局阶段完成后，在精确合法化中暴露分数列预算死循环。约 30.49 GB 完整重复日志无损保存为 `logs/amf.log.gz`（约 231 MB），另存首尾摘录。终止状态和原因保留。
- `getrf-u250-full-20260926-170252-047980`：包含列溢出修复的新构建 `build-20260926-165756-043079-fbebbe04` 已越过原始列预算死循环，但减少前期迭代导致末段 QP 发散：线长上升至 1.0315e11 后产生 NaN 坐标，2026-09-26 17:29:16 自行退出。失败配置仅保留在实验目录，不作为推荐入口。
- `getrf-u250-full-20260926-173107-822886`：恢复标准 30 次配置，AMF 3125.938 s 完成并成功导出；但覆盖检查发现 298 个 MUXF7、91 个 MUXF8 未输出 BEL。保留该轮 Vivado 诊断，不计作修复后的完整 AMF 验收。

- `getrf-u250-full-20260926-183758-400040`：使用构建 `build-20260926-183632-669176-fbebbe04` 原生包含 MUX 试插入状态修复与独立 SRL Q31 出口修复。AMF 3411.106 s、退出码 0；856,998/856,998 个真实单元全部赋值，MUXF7 13,860 个、MUXF8 5,679 个无遗漏。325 条 SRL 级联检查通过，适配层未修改任何站点或 BEL。Vivado 后端正在验证。构建二进制 SHA256 为 `c2e3fddc6cb89c56bd9cc67b33b17011fb29b5d1c3ab0b23e382d6299b7f6df8`，全部 3,387 个源码文件与当前原生代码一致，证据 `experiments/evidence/getrf-u250-source-match-20260926-1847.json`。

- `getrf-export-adapter-20260926-192606`：444 MB 真实 Tcl 的增强适配预检通过，准确拒绝旧输出缺失的 389 个 MUX；Tcl 语法完整，新增诊断前后请求位置清单 SHA256 一致。

## 入口与结果语义

```sh
python3 scripts/amf3.py build --jobs 8
python3 scripts/amf3.py full-run
python3 scripts/amf3.py full-run --packing-only
python3 scripts/amf3.py validate-packing --packing-run experiments/runs/<packing-run>
python3 scripts/amf3.py full-run --placement-run experiments/runs/<completed-amf-run>
```

每次运行保存 config、manifest、status、二进制/输入/构建哈希和分阶段日志。已完成 AMF 的实验即使后端失败，也可通过 `--placement-run` 在新目录重试；不会复用未完成或只做初始打包的轮次。后端从 AMF 生成的放置 Tcl 提取赋值，仅分离导入、`place_design` 和 `route_design` 以计时，并修正旧 Tcl 的 `$errorNum` 命令输出问题以及最后一个失败批次的带索引名字转义；保持 AMF 分配的站点；若遇旧版本独立 SRL 的 H6LUT/Q31 导出，则进行上述显式记录的同站点 BEL 纠正，不把这些改动计作原始 BEL 精确复用。

`amf_coverage.json` 记录每种单元的输出覆盖率；若有真实单元缺少赋值，适配入口在启动 Vivado 前失败，不把后端补放当作 AMF 的完整导出；`imported/placed/routed_placement.json` 分别记录导入后、后端布局后和布线后位置匹配；`*_cascades.json` 检查 Carry/DSP 的同 SLR 连续性。工具退出、布线完整性、DRC 错误、时序收敛分别报告，不相互代替。汇总中的 `implementation_verified` 同时要求 AMF 全覆盖、完整布线、DRC 无错误和布线后级联无违规；时序仍由独立 `timing_met` 判定。硬资源文件使用独立阶段的 v1 schema，完整 AMF 阶段是否执行以本轮 manifest/status/summary 为准。

## 后端环境预检

`u250-backend-probe-20260926-1801` 使用微小原生 Vivado 计数器验证同一 U250 型号的 OOC 综合、布局、布线和许可，146.003 s 完成，10/10 可布线网络完成、路由错误 0。这不是 GETRF 或 AMF 全流程验证。OOC 时钟未设置顶层 `HD.CLK_SRC` 时，Vivado 提示不能估计时钟延迟/偏斜；最终 GETRF 报告单独记录该提示，不把 OOC 约束结果等同于顶层时钟树签核。
