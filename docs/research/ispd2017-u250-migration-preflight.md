# ISPD2017 / OpenPARF Table IV 算例的 U250 迁移预检

日期：2026-09-29。服务器：eda072，Vivado 2024.2。服务器源码提交为 `c70df682d8b4bdbe9b71b56008d1087d52ddad51`，工作区另有既有修改，本轮未修改 AMF 算法或构建。

用户最初要求保留各算例原有时钟约束；查明原包没有目标周期后，明确选择“先完成迁移预检，暂不启动正式布局布线”。因此，本轮没有执行 AMF placement、Vivado `place_design`、`route_design` 或 bitstream 生成，也没有添加 10 ns 等新周期。

三个候选均已通过 U250 输入迁移预检，生成 `xcu250-figd2104-2L-e` 的未布局 DCP。原始与迁移后单元名称/类型、端口身份一致，黑盒数均为 0；所有 Bookshelf 单元均保留，每例另有原 DCP 中的 2 个 GND 和 2 个 VCC 常量单元。最终 ZIP CRC 与 SHA-256 校验完成。

| 算例 | Benchmark 单元数 | DCP 逻辑叶单元数 | 输入迁移总耗时 | 退出码 |
|---|---:|---:|---:|---:|
| CLK-FPGA08 | 469,497 | 469,501 | 108.05 s | 0 |
| CLK-FPGA04 | 682,292 | 682,296 | 151.81 s | 0 |
| CLK-FPGA06 | 937,412 | 937,416 | 208.52 s | 0 |

此处耗时包含打开源 DCP、核对身份、EDIF 导出/重读、U250 链接及报告/DCP 写出，是迁移预检耗时，不是 AMF placement 或 Vivado 后端耗时。

## 候选算例

选用公开 ISPD2017 数据包中的三个原始 DCP，覆盖不同规模。它们属于 LEAPS / OpenPARF 2.0 Table IV 使用的电路集合；这里进行的是将电路迁移到真实 U250 器件的输入预检，不能当作论文已提供 U250 布线结果。

| 算例 | LUT | FF | RAMB36E2 | DSP48E2 | BUFGCE | 顶层 I/O |
|---|---:|---:|---:|---:|---:|---:|
| CLK-FPGA08 | 212,249 | 256,648 | 161 | 75 | 32 | 332 |
| CLK-FPGA04 | 309,134 | 371,979 | 467 | 224 | 44 | 444 |
| CLK-FPGA06 | 424,566 | 511,038 | 872 | 420 | 58 | 458 |

资源数由原始 `design.nodes` 统计，不把 Vivado 展开的 DSP/IBUF 内部子单元重复计入。

## 原约束和兼容性

13 个原始 DCP 均为 Vivado 2016.4、`xcvu095-ffva2104-2-e`。其内嵌 XDC 只含位置、封装引脚和来源信息属性，没有 `create_clock` / `create_generated_clock`。三个候选算例经 Vivado 实际打开后，`get_clocks` 都返回 0；`write_xdc -type timing` 也未导出周期定义。32/44/58 是 BUFGCE 网络数量，不是已定义时序目标的数量。

Vivado 2024.2 直接打开原 DCP 时，旧 `.incr` 缓存触发 `Designutils 20-1451` 和 `Restore timing info for incremental flow failed`。原文件保持不变；派生兼容副本仅移除 `INCR` 文件及 `dcp.xml` 中相应条目。其余成员，包括 EDIF、XDC、XDEF，逐项校验为字节一致。原包及兼容副本都有旧格式完整性警告，不能声称该警告本身已通过厂商校验。

兼容副本的生成工具为 `scripts/diagnostics/prepare_ispd_checkpoint_compat.py`。已使用 CLK-FPGA08 验证该工具产物的所有归档成员与成功打开的兼容输入完全相同。

## 迁移与核对方法

1. 打开兼容副本，保存原始逻辑叶单元身份、端口身份、物理 XDC 和时序 XDC。
2. 清除 VU095 的 `PACKAGE_PIN`、`LOC` 和 `BEL` 绑定。保留逻辑电路、I/O 缓冲器、时钟缓冲器及 LVCMOS18 属性。
3. 使用 Vivado `write_edif` 导出可移植网表，再用 `read_edif` / `link_design` 导入 `xcu250-figd2104-2L-e`，恢复原时序 XDC。
4. 核对迁移前后逻辑叶单元的名称和类型、顶层端口身份、时钟对象数和黑盒数，再生成 `u250_unplaced.dcp`。
5. 最终审计对照 Bookshelf 单元集合，核对 DCP 的器件、ZIP CRC 和 SHA-256。

这些检查验证输入结构保留与工具可读性，不是形式功能等价证明，也不证明可布通、时序收敛或可直接上板。

## 后续正式实验的前置工作

- 决定目标周期和时钟关系。原包无周期；不能把 AMF 内部缺省的 10 ns 说成原设计约束，也不能报告当前 WNS 通过。
- 确定板级接口或 OOC 实验范围。本轮未加载 U250 板级 pinout、Vitis shell 或用户平台约束；迁移后的端口没有 package pin。现有 LVCMOS18 属性不等于已经满足板卡接线。
- 补齐 AMF 的固定 I/O / BUFGCE 资源信息。当前 GETRF 的 U250 器件导出仅包含 SLICE、DSP、BRAM、URAM；虽然 AMF 能识别 IBUF、OBUF、BUFGCE 类型，原 VU095 的固定位置也不能直接用于 U250。不能直接套用 GETRF 配置并声称这些资源已合法化。
- 正式运行时继续采用项目约定的 legacy 匹配器，并单独记录 DCP→AMF、AMF、AMF→Vivado、Vivado placement 和 routing 时间。本轮未执行这些阶段。

## 证据与产物

原始输入位于服务器：

`/Projects/jinyang/workspace/AMFplacer3.0/data/reference/openparf-benchmarks/table-iv/ispd2017/`

原始约束与兼容性预检：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20260929-ispd-u250-cache-compat-171627/`

U250 输入迁移及最终审计：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20260929-ispd-u250-retarget-172142/`

每个算例子目录保存 `u250_unplaced.dcp`、网表、身份清单、约束与资源报告；根目录保存 manifest、工具日志、状态、输入/脚本来源及 `preflight_summary.json` / `SHA256SUMS`。DCP 和大型网表只保存在服务器。本地仅同步轻量报告。

失败尝试保存在同级 `20260929-ispd-u250-constraints-170952`、`20260929-ispd-u250-netlist-171400`、`20260929-ispd-u250-netlist-171521`、`20260929-ispd-u250-retarget-171915` 目录，不作为成功结果。原包内部 `.edf` 是 checkpoint 私有网表格式，不能直接当标准 EDIF 读取；正确流程需要由成功打开的设计执行 `write_edif`。
