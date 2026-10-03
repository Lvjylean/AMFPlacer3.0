# ISPD2017 / U250：r10 参数、10 ns 实验

2026-09-29 用户要求以 10 ns 从小规模开始，采用 r10 配置。首例为先前选择的三个算例中最小的 CLK-FPGA08：212,249 LUT、256,648 FF、161 BRAM36、75 DSP、32 BUFGCE、332 I/O，共 469,497 个非恒定单元。原始 DCP 没有目标周期；10 ns 是本轮用户指定值，不能记作论文原始时钟约束。

## 当前运行

- 服务器项目根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。
- 正式运行：`experiments/runs/clk-fpga08-u250-r10-10ns-compat-full-20260929-185326-239497`。
- 当前状态：AMF 完成、全部单元通过严格导入；Vivado 后端因时钟路由容量超限失败。修正为 30 个真实时钟后重试仍得到相同容量超限，尚无全量布线结果。
- r10 参考：`experiments/runs/getrf-u250-full-20260929-133345-334380`。
- 新构建：`builds/build-20260929-185044-227956-c70df682`。
- AMFPlacer SHA-256：`087dc49db826304f551809fe6c75d30db85ea4014ec8850eedd1741518d94bd7`。

配置保留 r10 的全部算法参数：`ClockPeriod=10`、`y2xRatio=0.4`、SA 有效比例 0.32、`GlobalPlacementIteration=30`（入口参数，不代表所有内部循环总共 30 次）、二维物理区域聚拢、`SLRBoundaryDelayNs=1.5`、`BipartiteMatchingBackend=legacy`、`MacroCandidateSelection=cached_topk`、`DSPCritical=true`。启用与 r10 相同的功能耗时采集。没有修改默认构建链接。

## 输入适配及验证

输入基于已完成的 U250 迁移 DCP，器件为 `xcu250-figd2104-2L-e`。共有 32 个 BUFGCE 全局网络，但实际时钟是 `clk1`–`clk30` 的 30 个域。`BUFG_inst0` 的根 `ip_300` 驱动复位，`BUFG_inst1` 的根 `ip_301` 驱动使能等控制引脚，不是真实时钟。初版准备脚本误将所有 BUFG 输入都创建为时钟；后续通过完整叶子引脚连接及 Vivado `IS_CLOCK` 属性复核，单独派生 DCP 撤销这两个控制网络上的 `create_clock`。保留 30 个真实时钟的 10 ns、相位 0 目标；未添加异步时钟组、false path 或未知的板级 I/O 延迟。旧失败轮次及原约束保留以便追溯。

Vivado `place_ports` 为 364 个 I/O/BUFG 单元生成合法位置。随后固定这些输入位置；独立检查验证旧 AMF 导出器的 `place_design -unplace` 之后，可完整恢复全部 364 个单元的原 LOC/BEL。物理引脚是器件级自动分配，尚未对应 U250 板卡的实际引脚或平台 shell，不能称为可直接上板的设计。

完整层次网表导出得到 481,808 条逻辑网和 409,737 个 BUFG 全局网负载端点。AMF 加载后得到 481,806 条网；恒定网络合并方式不同，单元数量仍为 469,497、AMF 按既有全局网络处理方式登记的 BUFG 网络为 32（其中 30 个是真实时钟，另 2 个为全局控制）。原始 GND/VCC 共 4 个单元由输入转换器合并为恒定网络。

r10 原器件文件仅含 fabric 站点。本轮追加 U250 全部 1,216 个 HPIOB/BUFGCE 站点，并补充 IBUF、OBUF、BUFGCE 的兼容性表项。原有 fabric 站点记录、物理区域容量、边界和惩罚均不变。必须补齐整列站点，才能满足旧读取器“每一行时钟区域边界一致”的要求。追加真实站点后，AMF 按站点包络推导的 X3/X4 时钟区域分界从 75.25 变为 74.5；物理 I/O 带边界仍是 74.75。此差异属于器件输入适配，已写入 provenance。

输入目录：`experiments/preflight/20260929-clk-fpga08-u250-r10-export-180616`。正式配置为其中的 `r10-10ns-dsp-metadata-config.json`，完成 AMF 布局的来源记录为 `compat_v2_input_provenance.json`。全器件固定资源扩展位于 `experiments/preflight/20260929-u250-fixed-resource-sites-181309/device-r10-fixed`。

## r10 二进制兼容性问题

原 r10 二进制已实际尝试，运行 `clk-fpga08-u250-r10-10ns-full-20260929-181608-224337` 在时序图初始化时退出，未执行布局。具体环路为 DSP 的 `P` 输出经过两级 LUT 反馈到其 `CEM` 输入。旧模型把 DSP 当作组合节点，因此形成假环路。该路径经过的 DSP 实际为 `PREG=1`；Vivado `LUTLP-1` 检查为 0。

Vivado 属性与完整连接表进一步证明：本例全部 75 个 DSP 都是 `PREG=1`，且实际使用的输出均为 `P[47:0]`。新增可选 `DSP registered outputs file` 输入仅为这种已由 DCP 证实的情况设置既有 `hasDSPReg` 标志。读取器拒绝未知单元、重复记录、PREG=0、非 P 输出、越界 P 位和空输出。混合寄存/旁路输出不在这一兼容修正的支持范围内。

首次兼容构建仅改变 `InitialPacker.cc`，新增 `DSPRegisteredOutputs.h` 及对应原生测试。有效用例和 9 类拒绝用例通过。修正后真实算例的时序图完成前后向分层，深度为 6，进入正式聚类。

随后运行 `clk-fpga08-u250-r10-10ns-dspmeta-full-20260929-182518-843451` 暴露第二个边界情况：本例没有 Carry/LUTRAM，旧 `MacroLegalizer`、`CLBLegalizer` 在空目标集合上保留位移哨兵值 10000，持续阻止 `macroCloseToSite` 和 `macroLegalizationFixed` 收敛。这一运行在明确诊断后由 SIGTERM 终止，原因及 PID 记录于其 `reports/termination_reason.json`，未进入后端。

最终兼容构建让已确认空目标的粗略/精确位移查询返回 0，并在每次目标扫描时更新 `noTarget`。尚未检查的状态仍保留原哨兵，非空合法化阈值和算法保持不变。新增原生测试使用 CLK-FPGA08 真实输入，验证未知状态、空 Carry/MCLB 的粗略/精确及重复合法化，全部通过。同时补齐空目标集合的诊断导出保护，避免 Carry 诊断访问空列结构；真实输入的强制诊断导出回归通过。相对 r10 的额外源码差异为两类合法化器的 `.h/.cc`、测试及 CMake 测试目标；完整文件差异写入 provenance。此次实验属于“r10 参数 + 两项输入兼容修正”，不是原 r10 二进制的直接复现。

## 验收与耗时

须分别记录 AMF 功能布局时间、AMF 进程墙钟、DCP 导出/转换、AMF→Vivado 适配/导入、Vivado placement、route、报告及 DCP 写出。原始 DCP、迁移输入与最终 DCP 均留在服务器。布局覆盖、严格导入、完整路由、DRC、时序及原位置保留率分别验收。

已测输入导出：161.321 秒；连接表转换：9.624 秒；时钟/I/O 准备：65.772 秒。输入检查、DSP 诊断和失败准备尝试单独保留，不混入 AMF 布局时间。正式后端的历史输出文件名仍为 `getrf_routed.dcp`，实际算例身份由运行目录、manifest 与输入哈希确定。

## 已完成的 AMF 与后端诊断

- AMF 实际布局：385.550 秒（6.426 分钟）；AMF 进程墙钟：440.776 秒（7.346 分钟）。输入读取、诊断输出、验证及格式导出等未计入纯布局耗时。
- 输出覆盖：469,497 / 469,497；其中 AMF 新分配 469,133 个 fabric 单元，恢复 364 个输入中已固定的 I/O/BUFG 单元。严格 Vivado 导入的 LOC/BEL 匹配为 100%。
- 原始加权 HPWL：1,239,881.580（r10 坐标；不是布线后线长）。
- AMF 最后一次粗略区域时钟占用审计已经显示峰值 32，高于代码中的 24；最后 `checkClockUtilization(true)` 的返回值没有阻止 `Placement Done`。这证明进程成功不能代表时钟路由合法。
- 首次后端固定 BUFG 源，报 `Place 30-838`：两个时钟源共用轨道且负载区域重叠。
- 后端重试 `clk-fpga08-u250-r10-10ns-clock-backend-full-20260929-190919-076910` 复用已完成的 AMF 布局，只允许 Vivado 重选 32 个 BUFGCE 的站点；其余 AMF 位置保持严格审计。该轮仍报 `Clock count is 25 at the region (4 : 6) which is more than allowable quantity`，随后 `Place 30-744` / `30-99` 时钟路由失败。
- Vivado 首条超限报错中的计数为 25，但随后完整区域网络覆盖表在 X4Y6 列出 32，与 AMF 最终矩阵该位置的值一致。不能把首条报错中的 25 当作完整区域占用。两个全局控制网络虽然不应定义为 STA 时钟，仍通过 BUFG 使用全局路由资源。

本组 AMF 后端没有完成 `route_design`，没有最终 routed DCP，也没有可用于宣称 10 ns 收敛的 WNS/TNS。保持 r10 参数不变，先把本例的失败原因及修正约束复核清楚，再扩大算例规模。

## 最终复核结果（19:29）

最终后端运行：`experiments/runs/clk-fpga08-u250-r10-10ns-30clocks-full-20260929-192441-680361`，状态 **failed**，停止于 `place_design` 时钟路由，未进入 `route_design`。正确的 30 个时钟均为 10 ns，仍报 X4Y6 的 25 路全局网络超限。该轮仅重跑后端，没有重跑 AMF，也未允许 Vivado 移动 AMF 的逻辑单元以掩盖布局问题。

约束修正输入及来源：`experiments/preflight/20260929-clk-fpga08-timing-correction-192214/corrected/amf_input_30clocks.dcp` 和同级上层 `corrected_input_provenance.json`。Vivado `check_timing`：no_clock、unconstrained_internal_endpoints、multiple_clock、loops 均为 0；201 个输入和 100 个输出缺少板级延迟，未凭空补充。

约束修正前后重新导出复核：469,501 个规范单元（包括 4 个恒定单元）的清单完全相同；481,808 条逻辑网络忽略导出时生成的网络编号和行序后，驱动及负载端点记录全部一致，差异为 0；固定单元与固定站点清单 SHA-256 完全一致。DCP 内部不透明 EDIF 序列化字节和网络导出编号发生变化，因此使用规范连接记录进行比较，不以 DCP 文件哈希相等作为复用依据。这不是 RTL 形式等价证明。

| 项目 | 结果 |
| --- | --- |
| AMF 纯布局时间（复用源运行） | 385.550 秒 |
| AMF 进程墙钟（复用源运行） | 440.776 秒 |
| 本轮 AMF→Vivado 适配 | 5.541 秒 |
| 本轮打开 DCP | 38.857 秒 |
| 本轮导入 AMF 位置 | 93.993 秒 |
| 本轮严格导入审计 | 7.232 秒 |
| 全部请求位置匹配 | 469,497 / 469,497（100%） |
| 本轮 Vivado 进程墙钟（含失败） | 249.666 秒 |
| route / 最终 WNS、TNS / routed DCP | 未执行 / 无可验收结果 / 未生成 |

`place_design` 失败时旧 `timed` 包装器没有写出该阶段独立耗时；保留 Vivado 原生日志中的耗时（秒级精度）作为证据，不能把整个 Vivado 进程时间称为布局时间。格式转换、约束修正和独立连接复核也没有计入 AMF 纯布局时间。

最终报告为该运行的 `reports/acceptance_result.json`、`reports/ispd_r10_metrics.json`、`reports/r10_config_comparison.json` 和 `reports/input_validation/connectivity_recheck.json`。源码/构建及原始输入哈希留在 manifest；全部 DCP 留在服务器。后续优先处理时钟区域容量与最终时钟合法化，再扩大到 CLK-FPGA04、CLK-FPGA06；当前不把本例记录为成功布线或 10 ns 时序收敛。

## 后续 Vivado 原生对照（21:08）

按用户要求，另开同一输入 DCP、同一组 332 个 I/O 封装引脚、30 个 10 ns 时钟的原生 Vivado 运行，解除 AMF/fabric 位置并让 Vivado 自行布局布线。运行 `clk-fpga08-u250-vivado-10ns-20260929-194539-494632` 已完整布通 398,083 / 398,083 个可布线网络，DRC Error / Critical Warning 均为 0，输出 routed DCP 留在服务器。逻辑实际跨 SLR0、SLR1，最终使用 2,024 条 SLL。

最终 WNS +0.832 ns、TNS 0，但 WHS −2.834 ns，仍有 16,354 个 hold 违例端点；不能称为 10 ns 全部时序收敛。这一对照证明先前 AMF 布局的时钟容量失败不能解释为该算例在 U250 上根本无法布通，也不改变本组 AMF 后端失败的结论。详见 [Vivado 原生实验记录](ispd2017-u250-vivado-baseline.md)。
