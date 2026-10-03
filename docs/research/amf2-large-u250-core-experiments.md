# AMF2 大算例的 U250 核心实验

本轮目标：先取得三个公开大算例针对 `xcu250-figd2104-2L-e` 的综合与 post-opt 网表，全部完成输入检查后，采用原 AMF 周期、r10 算法配置执行 AMFplacer3.0 布局和 Vivado 后端布线。

服务器主目录：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20260930-amf2-large-u250-cores-01`。所有 DCP、EDIF 和大型网表保留在服务器。

截至 2026-09-30 05:31:40（香港时间），三个网表全部准备完成，三个正式 AMF 布局和对应 Vivado 后端均已完成。完整布线率均为 100%，DRC 错误和 Critical Warning 均为 0。

| 算例 | AMF / Vivado 周期（ns） | 最终 WNS（ns） | 最终 WHS（ns） | 实际 SLL | 当前 OOC 时序 |
|---|---:|---:|---:|---:|---|
| MiniMap2 | 8 / 8 | −4.094 | +0.019 | 10,198 | setup 未满足 |
| OptimSoC | 20 / 19.992 | +4.526 | +0.030 | 695 | 满足 |
| MemN2N 初始化修复版 | 10 / 9.999 | −3.444 | +0.028 | 3,903 | setup 未满足 |

所有结果均有下文的 OOC 时钟源和接口约束限制；“满足”只表示本次核心模型内已约束路径满足要求。

## 范围与周期

选择公开源工程中配置可追溯的三个大算例。Gemmini 的原 AMF 网表较大，但发布包没有配套完整 RTL/Vivado 工程。OpenPiton 原发布 DCP 只有两个 tile，而包内当前 RTL 生成了四个 tile；四 tile 派生网表单独保留，本轮正式三例使用下一档 MemN2N。

| 算例 | 原 AMF 全板 cells | 新 U250 AMF cells | 本轮核心边界 | AMF 目标周期 | Vivado 核心周期 |
|---|---:|---:|---|---:|---:|
| MiniMap2 | 681,889 | 670,553 | `device_chain_kernel`，AXI Stream/AXI Lite 边界 | 8 ns | 8 ns |
| OptimSoC | 468,150 | 414,868 | `system_2x2_cccc_dm`，4 tile、每 tile 4 核，GLIP/Wishbone 边界 | 20 ns | 19.992 ns |
| MemN2N 初始化修复版 | 289,721 | 177,419 | `memory_network_top`，原 FIFO 接口及常量绑定 | 10 ns | 9.999 ns |

AMF 周期来自原始 `original_config.jsonc`；Vivado 周期来自同一公开案例原 DCP 中驱动对应核心的时钟。二者分别记录，未将全部域统一为 10 ns。

这是一组 **U250 计算核心 OOC 基线**。MiniMap2 排除 PCIe DMA shell；OptimSoC 排除板级 DDR、GLIP UART transport 和 Wishbone/AXI 适配器；MemN2N 排除 PCIe/Xillybus、主机 FIFO 和板级时钟生成器。计算核心参数保留；MemN2N 另有下述权重初始化修复，使用独立 RTL 副本。它不是论文整板设计逐项复现，也不构成可直接下载至板卡的完整系统 bitstream。

只比较已约束的核心内部寄存器路径。OOC 边界没有虚构板级 input/output delay，报告会保留这部分未约束 I/O 数量，不能据此声称接口时序闭合。内部 `no_clock`、`unconstrained_internal_endpoints`、组合环和黑盒需要独立检查。

OOC 输入时钟未指定来自实际父层设计的 `HD.CLK_SRC`。三个后端均明确报告 `[Route 35-197]`，因此 WNS/WHS 是当前 OOC 边界模型下的结果，不能用来声称完整 U250 时钟树或板级时序签核。后续若比较可上板系统，必须为双方提供同一平台 shell、物理时钟源及真实 I/O 预算，重新验证时钟放置和时序。

## 综合与追溯

- Vivado 2024.2，每个综合作业最多 4 线程，三个独立目录可同时综合。
- 从公开包原 synthesis Tcl 提取源文件及编译宏，将旧作者路径映射至解压目录并记录 SHA-256。
- MiniMap2 延用原 `flatten_hierarchy full`，另两项延用原默认 `rebuilt`。
- OptimSoC 必须保留原脚本的 `NUM_CORES=4`，不能误用板顶层默认 1。
- 原工程只读。保存 post-synthesis DCP、post-opt DCP/EDIF、完整 timing XDC、资源和时钟报告。
- MemN2N 保留 `define.h/common.h` 的原配置和全部权重 `.mem`。除法器 `div_32` 包在 `.xcix` 中；从其 XCI 提取全部 `PARAM_VALUE`，用 Vivado 2024.2 针对 U250 重新生成 Divider Generator 5.1，保留 High_Radix、signed、32/32 位、32 位 fractional、NonBlocking 和手动 latency=6。原 IP 容器只读，生成后的属性报告另存。
- MemN2N 原发布 `MemN2N.runs/synth_1/runme.log` 已有 32 条 `[Synth 8-4445]`，表示 `$readmem` 文件打不开；本轮 `memn2n-retry03/console.log` 也是 32 条。此前将原日志数量记为 96 条不准确，已按单份原始日志复核更正。`memn2n-retry06` 在独立 `derived-rtl` 中把文件名数字显式转为 8 位字符，并定义 `Q_FORM_Q_7_24` 以选择缺失的三个 embedding 权重文件名前缀；该宏仅用于文件名，与原 `BW_IWL=7`、32 位数据一致。五组共 160 个原始 `.mem` 数值不变，逐个记录哈希，并把缺失初始化文件警告提升为错误，最终日志中该警告为 0。本轮称为 **MemN2N 初始化修复版**，不能声称与论文原 DCP 等价；尚未进行端到端功能验证。
- MemN2N retry06 的综合和 `opt_design` 成功后，报告阶段因从旧尝试复制来的同名 `post_opt_timing.xdc` 而退出。`report-recovery` 重新打开同一个已校验哈希的 post-opt DCP，在空目录完成全部报告；保留原失败状态和旧报告，未重新综合或修改网表。
- OpenPiton 的额外派生尝试：前两次综合发现发布包缺少正式 include。第三次在 `openpiton-retry03/upstream-headers` 补入 `iop.h`、`sys.h`、`tlu.h`，均来自 [OpenPiton 官方固定提交](https://github.com/PrincetonUniversity/openpiton/tree/aeb8e684de51f328ba830de0f2762e8494bcc5d0/piton/design/include)。校验 Git blob 和 SHA-256，保留失败尝试；不修改原 RTL 或编译宏。其新网表有 455,538 个原语，含 tile0–tile3；原 AMF 网表只有 tile0（112,000 cells）和 tile1（111,758 cells），其余大部分是 chipset。两者不能混称为同一个论文 case。本轮不对这个四 tile 派生版本执行正式布局布线。

## AMF 与验收

配置从已完成 r10 的 `experiments/runs/getrf-u250-full-20260929-133345-334380/config.json` 派生，仅替换各案例输入、输出和目标周期。保留共享 y2xRatio=0.4、原 SA 比例规则（有效 0.32）、tile 列坐标、二维边界聚拢、SLRBoundaryDelayNs=1.5、legacy matching、cached_topk、GlobalPlacementIteration=30、DSPCritical=true。

使用冻结的兼容 profiling 构建 `builds/build-20260929-185044-227956-c70df682/build/AMFPlacer`，SHA-256 `087dc49db826304f551809fe6c75d30db85ea4014ec8850eedd1741518d94bd7`。其相对于 r10 的已记录兼容补充包括 DSP 寄存输出识别和空宏集合保护，算法配置相同。

所有网表导出绑定同一 U250 post-opt DCP 哈希，保留唯一单元名、引脚连接、完整时钟负载。仅对 DCP 证实 `PREG=1` 且全部内部使用输出为 `P[47:0]` 的 DSP 写入寄存输出元数据。调用标准 `amf3.py inspect` 后才允许进入全流程。

MemN2N 的优化网表自动插入了一个 `BUFGCE`：`memory_network_top_module/memory_network_control/p_1_out_BUFG_inst`，驱动 33,760 个 `CLR` 和 2 个 `PRE`。它是全局复位控制网络。导出器 v6 以缓冲器输出的真实下游引脚判断时钟，避免把 `BUFGCE/I` 自带的 `IS_CLOCK` 属性误当作新增时钟域。核心 STA 仍只有 `clk`。为保留该物理缓冲资源，复用先前已校验的 r10 固定资源扩展（追加完整 I/O/BUFG 站点、原 fabric 记录与物理边界容量不变），其初始锚点为最接近 fabric 包络中心的 `BUFGCE_X0Y192`。严格导入之后仅释放这个缓冲器的位置供 Vivado 重选，其余 AMF 指定位置继续审计。此输入适配及器件模型哈希记录于 `fixed-buffer-inputs/adapter.json`。

正式流程记录 AMF 退出码、严格导入、全量布线、DRC、setup/hold、SLR 分布与 SLL。进程退出 0 不等于 timing closure。正式布局布线必须等待三个选定网表全部通过准备检查。

最终结果以各运行的 `status.json`、`manifest.json` 和 routed 报告为证据，并汇总于正式批次的 `reports/results.json`。三个最终运行的 `implementation_verified`、`strict_import_verified` 和 `routing_complete` 均为 true；AMF 和成功 Vivado 后端的退出码均为 0。

## 输入与运行索引

最终三例 catalog 为预检主目录下的 `catalog-v12.json`。MiniMap2 输入目录是 `minimap2/amf-preparation-v5`，OptimSoC 是 `optimsoc/amf-preparation`，MemN2N 是 `memn2n-retry06/amf-preparation-v6`。每项的 `post_synth.dcp`、`post_opt.dcp` 在其父目录。

正式批次目录：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/amf2-large-u250-core-batch-20260930-01`。`netlist_gate.json` 确认三个网表在第一项正式 AMF 运行开始前已完成，并检查 DCP/AMF 输入哈希、原周期、r10 配置差异和内部时序覆盖。初始队列为 MemN2N → OptimSoC → MiniMap2，串行执行；OptimSoC 的审计修正后端重试与后续 MiniMap2 有重叠，运行耗时不能当作隔离机器上的严格性能对比。

本地轻量准备证据：`local-reports/amf2-large-u250-core-batch-20260930-01/reports/preparation/input_index.json`；只读状态检查命令：`python3 scripts/diagnostics/inspect_amf2_u250_batch.py`（在服务器运行）。

本地最终汇总：`local-reports/amf2-large-u250-core-batch-20260930-01/reports/results.json`，其中记录每项最终服务器 DCP 的完整路径和 SHA-256。三个最终运行的报告、日志和配置已同步到本地相同 run-id 目录；未下载 DCP、EDIF 或大型网表。综合网表的完整路径和输入哈希见上述 `input_index.json`。

初始串行队列的原始 `status.json` 保留 `failed`，对应 OptimSoC 首次 RAM 宏审计失败；不能把它误读为最终布线失败。合并记录保留 `original_batch_state=failed`、该失败尝试和恢复关联，同时记录最终 `state=completed`。恢复后端未改动原 AMF 布局。

## 已完成：MemN2N 初始化修复版

运行：`experiments/runs/amf2-memn2n-u250-core-r10-full-20260930-034734-853102`。AMF 进程耗时 440.480 s；Vivado 完整后端 946.742 s，其中位置导入 205.482 s、`place_design` 47.036 s、`route_design` 502.845 s。

177,419 个单元严格导入匹配率为 100%，无拒绝、SRL 或硬资源级联违规。布线后仅预先允许释放的复位 BUFG 改变位置，其余 177,418 个单元保留 AMF 指定位置。159,298 条可布线网络全部布线完成，DRC 错误和 Critical Warning 均为 0。最终实际使用 3,903 条 SLL，连接 SLR1/SLR2（不是初始估计的 4,262 条，也不是几何 driver-sink 跨界边计数）。

OOC 模型下 WNS −3.444 ns、TNS −146.152 ns，181 个 setup endpoint 失败；WHS +0.028 ns、THS 0，hold 失败 endpoint 为 0。最差 setup 路径从 `count_hop/count_reg[1]` 到 `fc_w_sm/tree_adder_1st_stage[18].tree_adder_1st_reg[9][19]`，数据延迟 13.421 ns，其中布线 10.901 ns（81.224%），沿途跨越 SLR 六次。不能仅因布线成功就称该设计已满足原 9.999 ns 周期。

最终服务器 DCP：该运行目录下 `reports/getrf_routed.dcp`（文件名沿用标准后端，目录标识真实算例）。SHA-256：`de0da62ac59d8ccffd1c95b80d32bbb225e2fa9d7fa65773a35204f40be41c11`。本地只同步相同 run-id 下的报告与日志。

## OptimSoC 的 RAM 宏审计修正

初次运行 `amf2-optimsoc-u250-core-r10-full-20260930-041052-550375` 完成 AMF 布局，但严格导入审计停止：414,868 个单元全部在正确 SLICE，400 个 `RAM32X1D` 请求 H6LUT，而 Vivado 的主 `BEL` 属性为 G6LUT。实际 `place_cell` 拒绝计数为 0；日志中的 MUXF7 自动形状 INFO 消息不是此次退出原因。

独立诊断 `experiments/evidence/20260930-optimsoc-ram32x1d-anchor` 分别使用 H6LUT 和 G6LUT 放置该宏，确认两者都占用同一 G6LUT/H6LUT 对，内部 DP 位于 G6LUT、SP 位于 H6LUT。审计器增加严格限于此类型、方向、相同 SLICE 和完整实际双 BEL 集合的别名检查；逐个写出 `*_composite_bel_aliases.tsv`，另保留主 BEL 字面匹配数。错误类型、位置、单 BEL 或额外占用仍不能通过。3 项审计测试（含 9 组宏占用正反例）和 15 项全流程测试通过。

重试 `amf2-optimsoc-u250-core-r10-belaudit-full-20260930-043209-705136` 复用原 AMF 布局和相同输入 DCP，只修正审计，无 AMF 参数调整或重新布局。原失败记录保持不变，恢复运行通过批次的 `recovery_runs.json` 关联，最终合并状态写入 `reports/results.json`。

恢复后端的导入、放置后、布线后三次位置审计均为 414,868/414,868 匹配，其中 400 个 RAM 宏通过上述完整双 BEL 检查，其余 414,468 个主 BEL 字面匹配。323,359 条可布线网络全部完成，路由错误为 0。`route_design` 耗时 3027.645 s。正式时序报告为 WNS +4.526 ns、WHS +0.030 ns，TNS/THS 均为 0，setup/hold 失败 endpoint 均为 0；这是当前 OOC 约束下满足原 19.992 ns 核心周期的结果。

原 AMF 进程耗时 778.112 s；成功恢复的 Vivado 完整后端耗时 3541.572 s（另有原失败后端 168.414 s，不混入成功后端耗时）。最终流程于 2026-09-30 05:31:40（香港时间）完成，DRC 错误与 Critical Warning 均为 0，`timing_met=true`。最终 `reports/getrf_routed.dcp` 的 SHA-256 为 `f6a4687b5a310330ae9a0b7ad51b7e970bbe88393ae805347bb798fb89e41862`。

实际 SLL 使用 695 条（SLR0→SLR1 为 325，SLR1→SLR0 为 370）。大部分逻辑位于 SLR0，SLR0/SLR1 的 CLB 站点占用分别为 30,007/629，即各 SLR 容量的 55.57%/1.16%。这不是在四个 SLR 之间均匀分布的实验。

## MiniMap2 布线结果

运行：`experiments/runs/amf2-minimap2-u250-core-r10-full-20260930-042650-790874`。AMF 进程耗时 732.002 s；Vivado 完整后端 3035.336 s，其中 `route_design` 1660.824 s、额外 200 条路径的延迟样本导出 653.796 s。严格导入及布线后审计均为 670,553/670,553 个单元位置匹配。461,127 条可布线网络全部完成，路由错误为 0。DRC 错误和 Critical Warning 为 0；保留 DSP 输出/乘法寄存器未启用的 Warning，与输入 DSP 配置一致。

正式时序报告 WNS −4.094 ns、TNS −4603.630 ns，7,082 个 setup endpoint 失败；WHS +0.019 ns、THS 0，hold 失败 endpoint 为 0。最差路径从 `active_0_3_reg_5038_reg[52]` 到 `select_ln1503_161_reg_24636_reg[1]`，数据延迟 12.070 ns，其中布线 11.254 ns（93.240%），沿途在 SLR0/SLR1 之间跨越五次。当前布局在原 8 ns 周期下未达到 setup 时序。

实际 SLL 使用 10,198 条（SLR0→SLR1 为 6,376，SLR1→SLR0 为 3,822），区别于布线初期的估计值 13,135。逻辑分布于 SLR0/SLR1。SLR0 已占用 84.06% 的 CLB 站点；此数值是站点占用，不是 LUT 利用率（61.15%）。

流程于 2026-09-30 05:30:11（香港时间）完成，AMF 与 Vivado 进程均退出 0；`implementation_verified`、`strict_import_verified` 和 `routing_complete` 均为 true，`timing_met` 为 false。最终 `reports/getrf_routed.dcp` 的 SHA-256 为 `1a6bd4591384b6b7a3068f1d58ba7815e9950a82e9600b0dce215e8b33c0d451`。

## 失败与中止记录复核

以下区分输入准备、正式实现退出和时序目标；不把所有 `failed` 都解释成无法布局布线。正式 AMF 布局三例均退出 0；正式后端首次非零退出只有 OptimSoC。包括准备阶段时，三个算例均有失败或中止记录。

| 算例／阶段 | 记录与原因 | 归因及处理 |
|---|---|---|
| MiniMap2／网表导出 | `amf-preparation`、v2、v3、v4 的 exporter 均退出 143，旧进程因耗时过长被终止。v4 停留在 `<const0>`：113,630 个层级 segment、397,764 个 pins。 | 早期导出器的层级查询和端口查询不能有效处理这种规模；属于适配脚本性能问题。v5 预建顶层端口映射、批量查询，完整保留网络并成功导出。其综合与最终 AMF/Vivado 正式流程未失败。 |
| MemN2N／首次综合 | `memn2n/console.log`：`module 'div_32' not found`。 | 新核心工程的初版准备脚本只收集 RTL，漏掉 `.xcix` 内的 IP 依赖。提取原 IP 参数并针对 U250 重建。 |
| MemN2N／IP 生成重试 | `memn2n-retry02/console.log`：IP directory does not exist。 | 准备脚本未创建输出目录；属于本轮脚本错误，retry03 补齐后综合成功。 |
| MemN2N／初始化验收 | retry03 虽退出 0，但有 32 条缺失初始化文件警告；retry05 把同类警告升级为 ERROR 后停止。 | 原发布日志已有同类问题；不能接受未加载权重的网表。首轮文件名修复不足，retry06 同时修复字符宽度与前缀选择后无该警告。该版本与原论文输入的功能等价性尚未验证。 |
| MemN2N／报告导出 | retry06 的综合和优化成功，但 `post_opt_timing.xdc` 已存在，`write_xdc` 拒绝覆盖。 | 准备脚本把旧输出 XDC 一并复制到新目录。输入输出隔离不完整；打开原 post-opt DCP 在空报告目录恢复，无需重新综合。复制规则后来收窄至真正输入。 |
| MemN2N／时钟检查 | 初版准备结果：`Expected one core clock`，误出现第二个时钟。 | 导出器把复位网络上的 `BUFGCE/I` 时钟属性当成了真实时钟域。检查下游 33,762 个 CLR/PRE 负载后，v6 按真实接收引脚分类并通过。不是设计真的有新增时钟。 |
| OptimSoC／正式后端导入审计 | AMF 退出 0，Vivado 审计退出 2：414,868 个单元中 400 个 RAM32X1D 主 BEL 字符串不一致。 | 早期审计器把宏当成单 BEL 单元；实际 G6/H6 双 BEL 占用合法。修正完整占用检查，复用原布局完成后端。不是 `place_cell` 拒绝或 route_design 失败。 |

还出现过启动编排错误：MiniMap2 v4 与 MemN2N retry04 的启动命令引用尚不存在的 `catalog-v8.json`；续跑器曾在 `synthesis_status.json` 尚未生成时读取它，并曾重复创建已有 MiniMap2 v5 准备目录，触发 `FileNotFoundError` / `FileExistsError`。这些是本轮任务编排和重试处理不完整，不能计为新的综合或布局失败。后续续跑完成，但并未据此证明整个编排器对所有竞态和重试情况都已泛化处理。

最终 setup 未达标另计：MiniMap2 WNS −4.094 ns、MemN2N −3.444 ns，流程本身成功且全部可布线网络完成。对应最差路径的跨 SLR 往返和布线延迟占比见上文；严格保留 AMF 位置限制了后端通过重新放置单元来补救。尚未做这三个相同输入的原生 Vivado 全量放置对照，不能把时序差异单独归因于 OOC、某一算法参数或器件容量。
