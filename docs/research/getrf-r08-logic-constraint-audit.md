# R08 逻辑不变量与约束覆盖审计

审计完成时间：2026-09-28（Vivado 2024.2）。

**结论：已验证范围内未发现 AMF 引入的可见逻辑、连线或放置错误；完整功能等价尚未获证明，严格约束覆盖未通过，不能签署“R08 完整无误”。**

| 验收维度 | 状态 |
|---|---|
| 可见逻辑不变量与逐引脚连接 | PASS |
| AMF 严格导入与 OOC 物理实现检查 | 通过，仍有已记录的 Warning/Advisory |
| 完整功能等价 | NOT_PROVEN：受保护 INIT 不可见 |
| 现有 10 ns OOC 约束下时序 | 通过 |
| 完整接口与时钟环境的约束覆盖 | FAIL |
| 无条件整体验收 | NOT_PASSED |

本次发现的是证明与约束覆盖缺口，不能据此判定 placer 已破坏逻辑。

审计对象：`getrf-u250-full-20260928-160231-470820`，GETRF / U250 / 10 ns / shared y2xRatio=0.71 / SA=0.71 / BoundaryAwareClustering=false。

服务器证据根目录：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/evidence/20260928-getrf-r08-logic-constraints-194442-550403`

最终 DCP：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-160231-470820/reports/getrf_routed.dcp`

## 已完成的核查

| 核查项 | 结果 |
|---|---|
| 输入、导入后、placed、routed 的 primitive 身份/类型 | 934,775 个，零增删、零类型变化 |
| AMF 请求的 LOC/BEL | 856,998 个，三阶段全部精确匹配 |
| 独立重放的导入拒绝次数 | 0 |
| 原生 primitive 模型参数记录 | 2,406,287 条，输入与 routed 逐字节一致；隐藏空值单独标为未证明 |
| 原生引脚记录 | 11,575,949 个，方向、连接状态、反相属性、引脚名均一致 |
| 原生层次网段 | 7,283,646 个，全部端点集合一致，零增删或重接 |
| 原生顶层端口 | 447 个，方向与连接一致 |
| 主 EDIF 网络定义 | 1,666,576 个，零连接变化 |
| 主 EDIF 端点引用 | 4,999,823 个，参与全量比较 |
| 输入→导入后的属性差异 | 67,141 个 SOFT_HLUTNM 打包提示被清除，可见功能属性无变化 |
| 导入→placed 的属性差异 | NETLIST_CHECKSUM 更名 ECO_CHECKSUM，值均为 16f1819b |
| placed→routed 主 EDIF | 除导出时间戳外完全一致 |
| 需放置的逻辑对象 | 876,710 个，未放置 0，site errors 0 |
| 布线 | 925,706 / 925,706，routing errors 0 |
| 无豁免 DRC | Error 0，Critical Warning 0；仍有 Warning 与 Advisory |
| 独立 STA | WNS +0.019 ns，TNS 0；WHS +0.023 ns，THS 0；WPWS +4.300 ns |
| STA setup/hold 端点 | 各 607,963 个，失败端点 0 |

原生表中的主键重复数为 0。7,283,646 是包含层次边界的网段记录数，不等于 Vivado route status 的 3,257,293 个逻辑网络；其中 925,706 个需要实际布线。

主 EDIF 中的 485,269 个可见 LUT/FF/SRL INIT 与 326,094 个隐藏 INIT 合计 811,363 个，数量与原生对象参数清单相符。主 EDIF 不是受保护 IP 的完整内部网表。Vivado 同时生成了 37,129 个受保护的独立 EDN 文件；没有把这些文件仅凭名称相同就判为等价，也没有尝试解密。内部覆盖使用公开 Vivado 对象查询补查。

## 受保护 IP 的功能可见性缺口

已经导出的 2,406,287 条 primitive 模型参数记录，输入与 routed 文件逐字节一致。其中覆盖 441 个“primitive 类型/参数”组合；模型声明中只有 FDRE/FDSE 的仿真控制 MSGON、XON 不是 DCP cell 属性。

但有 **326,094 个 INIT 返回空值**，且全部位于浮点 IP 的受保护内部层次：

| 对象 | INIT 不可读数量 |
|---|---:|
| LUT1～LUT6 | 224,959 |
| FDRE | 101,120 |
| SRL16E / SRLC32E | 15 |

空值相同不是对应真值表/初始状态相同的证明，不能解释为全 0 或默认值。主 EDIF 对受保护浮点 IP 保留接口与配置，内部实现位于加密 EDN。原生查询已补查内部对象与连线，全部一致，但不能补出这些 INIT。37,129 个受保护 EDN 的文件名集合在四阶段相同，密文字节均不同；密文差异没有被当成功能变化或等价证据。

因此，即使所有可见连线完全一致，仍不得把本轮结论写成“完整功能等价已证明”。可以在明确接受“受保护 IP 保持功能的可信黑盒”这一假设下评价外围逻辑；若要求无此假设的证明，需要可用于验证的功能模型/网表，以及支持它的等价验证流程。本轮没有运行 SAT/LEC。

## 约束覆盖结果

四个阶段导出的有效 timing XDC 指令一致，时钟为 `ap_clk=10 ns`。输入、placed、routed 的 check_timing 结果一致：无时钟寄存器、未约束内部端点、多时钟寄存器、组合环和部分 I/O 延迟均为 0；缺少输入延迟的端口为 124，缺少输出延迟的端口为 250。

`report_methodology -no_waivers` 另报 384 条 TIMING-18，其中输入 124、输出 260。额外的 10 个输出属于常量输出；原始结果均保留，不将其中一项替代另一项。全部 447 个顶层端口已按连接核对：1 个时钟、124 个有逻辑用途的输入、8 个未连接输入、250 个普通输出，以及 64 个常量输出（54 个接 const0、10 个接 const1）。8 个未连接输入是 AXI 的 BID/BRESP/BUSER/RID/RRESP/RUSER，属于原输入网表已有的未使用接口。

`HD.CLK_SRC` 未设置。Vivado 明确指出 OOC 模式下不能据此估计时钟延迟/偏斜。DCP 中只有一个已定义时钟；CDC 报告虽然写着安全定时，但同时明确跳过无输入延迟的端口，因此不能作为完整接口 CDC 验收。

没有 false path、clock group、multicycle 或被忽略的 timing exception。三阶段的 5,704,674 条 disabled timing arc 记录逐条一致，原因为 constant、constant negative_unate 或 constant positive_unate，没有新增用户禁用弧。

输入 DCP 原本携带两条施加到内部浮点 IP cell 的 FAST 约束，Vivado 提示忽略；最终 DCP 打开时仍可见同类警告。这些不是本次 AMF 新增的约束。

OOC 模式还使 Vivado 明确提示部分 connectivity-based DRC 无法运行。当前物理合法性结论限于已运行的 OOC 检查，不等于平台集成或 bitstream 验收。

## 复核方法与边界

使用 Vivado 2024.2 打开同一输入和输出。输入 DCP 来自 Vivado 2023.2，读取时发生供应商的 primitive retarget；比较基准是被同一 Vivado 2024.2 解释后的输入。

独立重放只在审计目录执行 R08 导入脚本的字节副本。原 DCP、R08 实验目录与 placer 源码未被修改。所有 DCP 留在服务器，本地只保留轻量指标与报告。

这是结构不变量审计，没有执行 RTL 到门级或门级到门级的 SAT/LEC，也不证明浮点 IP 原始算法或 Vivado 本身正确。由于隐藏 INIT 的前提未满足，不能把“可见记录一致”外推成无条件的完整功能等价。单个 R08 案例也不能证明 placer 对所有设计正确。

后续系统级严格验收需要真实 AXI 接口输入/输出的 min/max 延迟预算、时钟关系及 OOC 时钟源信息，并明确复位的时序意图。不能用任意 false path 或随意设定 I/O 延迟来消除警告。

## 复现与证据

- `audit-results.json`：最终机器可读验收结论，明确区分 PASS、NOT_PROVEN、FAIL。
- `manifest.json`、`source-integrity-recheck.json`：输入、placed、routed DCP 路径、SHA-256 与工具版本。
- `ledgers/*classification.json`：主 EDIF 的逐阶段属性差异分类；完整差异与 SQLite 明细保留在服务器。
- `native-comparison.json`：所有原生参数、引脚、层次网段和端口的全量比较。
- `protected-init-coverage-gap.json`：326,094 个隐藏 INIT 的类型分布与定位示例。
- `constraint-comparison.json`、`port-coverage-details.json`、`disabled-timing-summary.json`：约束、全部端口归类与禁用时序弧核查。
- `placed_routed/routed/`：独立生成的 STA、无豁免 DRC、方法学、CDC 和 route/place status 报告。
- `inputs/tools-final/`、`audit-toolset.json`：最终冻结的审计脚本与 SHA-256；`logs/final-toolset-tests.log` 保存变异测试结果。

39 个独立 Vivado 导出/报告命令均成功。原生导出脚本的前两次尝试分别在标量空属性、无引脚网络的查询上停止，修复后完成了所有数据的导出和核对，失败日志保留。比较器已验证可检测 INIT 修改、重接线、缺失网络、primitive 类型变化、端口方向变化和未知功能属性增加；可接受时间戳、枚举顺序和已明确分类的打包提示变化。

DCP 和完整网表仅保存在服务器。本地 `local-reports/getrf-r08-logic-audit-20260928/` 是轻量证据镜像。审计期间另有二维聚拢实验运行，因此本次审计耗时不应作为 placer 性能比较数据。

## 完成严格验收还需要的输入

1. 获得支持验证的受保护 IP 功能模型/网表和等价验证流程，或者明确接受将同配置 IP 作为可信黑盒的证明边界；两种结论不能混用。
2. 给出真实平台的 AXI 输入/输出 min/max 延迟预算、时钟关系和 OOC 时钟源信息，并明确复位约束。对未使用输入、常量输出给出可追溯的范围说明。
3. 在补全环境后重新进行 STA、约束覆盖与平台级实现验收。现有 WNS +0.019 ns 只对应当前 OOC 条件。

## 工具与检查依据

- [AMD：write_edif 对受保护 IP 的 multifile 导出规则](https://docs.amd.com/r/2022.1-English/ug835-vivado-tcl-commands/write_edif)。本轮没有使用 logic_function_stripped。
- [AMD：SOFT_HLUTNM 与 LUT 打包提示](https://docs.amd.com/r/2021.2-English/ug949-vivado-design-methodology/Disable-LUT-Combining?contentId=l5qNUW4lldfBeMa2je8yMw)。清除该提示不能被当作真值表修改。
- [AMD：约束覆盖核查](https://docs.amd.com/r/2024.2-English/ug903-vivado-using-constraints/Reviewing-Constraints-Coverage)。检查例外的覆盖及忽略情况，不能只读 WNS。
