# MiniMap2 加密 IP 与功能正确性检查

**后续结果已更新：** 2026 年 10 月 3 日，补充读取受保护模块的原语配置和实际层次网络后，AMF3 与原生 Vivado 均通过相对共同输入的完整逐原语改写核验。在相同初始化、GSR 释放后、二值且忽略物理延迟的模型下，可以判定逻辑等价。详细范围和证据见同目录的 [MiniMap2 逻辑结构差异与等价性分析](minimap2-logic-equivalence-analysis.md)。以下保留首轮扫描及原始结构统计；首轮“尚未证明”的结论已由后续核验推进。

2026 年 10 月 3 日，对服务器现有 U250 完整设计的共同输入、AMF3 后端结果和原生 Vivado 结果进行只读检查。MiniMap2 计算核心的 RTL 未发现加密保护，但完整设计包含加密的 XDMA 和存储器 IP。首轮完成源码扫描和可见功能网表结构比较，**没有证明两个完整实现功能等价，也没有发现已被验证的功能错误反例**。

这里的“加密”指 RTL/IP 的代码保护，不是说 MiniMap2 的基因序列比对算法执行密码学运算。

## 加密检查结果

| 检查范围 | HDL 文件数 | 含保护标记的文件数 |
|---|---:|---:|
| U250 实验实际复制的核心及顶层 RTL | 32 | 0 |
| U250 工程生成的 IP HDL | 103 | 3 |
| 原始公开 MiniMap2 工程内 HDL | 227 | 2 |

32 个实际 RTL 文件中，26 个是原始 `ipshared/adc8/hdl/verilog/` 下的 HLS 核心源文件；输入清单记录它们全部未修改。扫描覆盖 `.v`、`.sv`、`.vhd`、`.vhdl`、`.vp`，检查 `pragma protect`、`protect`、`begin_protected` 标记，并保存逐文件 SHA-256。未发现标记的结论限定于这些已检查文件。

U250 生成工程中的命中文件为：

- `full_system.gen/sources_1/ip/xdma_0/hdl/xdma_v4_1_vl_rfs.sv`。
- `full_system.gen/sources_1/ip/xdma_0/ip_1/hdl/blk_mem_gen_v8_4_vhsyn_rfs.vhd`。
- `full_system.gen/sources_1/ip/xdma_0/ip_2/hdl/blk_mem_gen_v8_4_vhsyn_rfs.vhd`。

三个 DCP 导出的功能 Verilog 都保留一个加密区块。三个 DCP 的 `IS_BLACKBOX` 数量均为 0：这是“没有未解析设计黑盒”的证据，不能解释成“所有 IP 都是明文”。Vivado 会在导出网表时保留受保护 IP 的加密形式，见 [AMD IP 加密说明](https://docs.amd.com/r/en-US/ug1118-vivado-creating-packaging-custom-ip/Encrypting-IP-with-Vivado)。首轮没有尝试解密。

## 对照输入与检查方法

两条实现流程使用相同的 `xcu250-figd2104-2L-e` 输入，Vivado 2024.2，核心周期 8 ns。PCIe 参考时钟及其他派生时钟保持原有设置，并非把全部时钟统一成 8 ns。

| 对象 | DCP SHA-256 |
|---|---|
| 共同输入 | `ee59c6e8530654ef94bea10ef5380b9fd64c1c8616e1e1dcb900999b8f20e80e` |
| AMF3 最终结果 | `5f4c150e9ca80be320c60e017d46d060d46f066c63f28745db5f3e9a428c0cb2` |
| 原生 Vivado 最终结果 | `b7d47dc43648c0e1cf129ddb697cd53ee2b1f1bd7fba89ba6499002b7c4c647d` |

原生结果采用已经完成的恢复轮次：复用原生放置检查点，重新执行路由并成功保存最终 DCP，完成时间为 10 月 1 日 11:48。未使用此前没有保存最终 DCP 的失败轮次代替它。

对三个 DCP 分别执行 `write_verilog -mode funcsim`，比较同名模块内实例的类型、显式参数和端口连接，以及模块端口声明、连续赋值。排除注释与属性，按名称排序参数和端口关联。加密体不参与比较，不因它们来自相同输入就假定已验证等价。Vivado 附加的 `glbl` 仿真模板单独核对哈希。

功能网表的用途及其与带延迟网表的区别见 [Vivado 2024.2 write_verilog 文档](https://docs.amd.com/r/2024.2-English/ug835-vivado-tcl-commands/write_verilog)。首轮没有使用清除逻辑功能的导出选项。

## 实际结果

三份网表的顶层端口、方向、封装管脚及 I/O 标准快照完全一致；17 个时钟的名称、周期、波形、源管脚快照完全一致；`glbl` 模板哈希相同。

| 可见结构统计 | 共同输入 | AMF3 最终 | Vivado 原生最终 |
|---|---:|---:|---:|
| 实例记录数 | 679,816 | 680,396 | 680,093 |
| 可见模块定义数 | 29 | 29 | 29 |
| 未解析黑盒 | 0 | 0 | 0 |
| 导出加密区块 | 1 | 1 | 1 |

实例记录按导出文件内的模块定义计数，不是展开所有模块实例后的器件利用率统计。

相对共同输入，AMF3 最终结果增加 45 个 BUFGCE 和 535 个 FDRE；原生结果增加 34 个 BUFGCE 和 243 个 FDRE。可见同名实例没有删除或类型改变。AMF3 有 83,592 个实例的连接表达式发生变化，原生有 105,711 个。这些是保守的结构差异数量，不是逻辑错误数量：比较器尚未归一化复制寄存器、缓冲器、网名别名和跨模块新增连接。

AMF3 有两个 DSP48E2 的 `AREG`、`ACASCREG` 从 1 改为 0，同时 `CEA2` 和 A 输入连接发生变化。原生最终网表的对应参数没有变化。进一步核对 AMF3 后端日志，在第 1146、1147 行找到 `[Physopt 32-665]`：这两个 DSP 各有 17 个寄存器被移到外部。新增网表中恰有 34 个 `_psdsp` FDRE；物理综合汇总也记录 DSP Register 创建 34 个单元、修改 2 个单元。

这解释了两处 DSP 变化的来源：Vivado `place_design` 内部的物理综合做了寄存器外移。它不是“AMF 随意改了乘法器参数”的证据，但日志与数量对应本身也不是该变换的独立等价证明。后端即使没有显式调用 `phys_opt_design`，仍可能在放置和路由阶段执行物理综合。

直接比较 AMF3 与原生结果，有 521,781 个同名可见实例的类型、参数及连接表达式完全一致，另有 158,171 个同名实例存在连接差异，以及两边各自独有的实例。不能据此把整个设计判为相同，也不能把不匹配比例当作功能错误率。报告中的 `kernel_counts` 仅是实例名含 `device_chain_kernel` 的子集；扁平化会丢失部分核心实例的此前缀，不能拿这个子集代表完整核心覆盖率。

## 首轮结论及后续核验

首轮已核实输入一致、工具一致、外部端口与时钟快照一致，已定位可见结构差异及 DSP 变换的日志来源。**首轮尚未验证完整设计逻辑等价性；后续结果见本文开头的更新。** 未执行激励仿真、SAT/时序等价检查、SDF 仿真或上板测试；当时的结构比较脚本未证明所有复制寄存器、DSP 外移和加密体等价。

另外，AMF3 的 routed WNS 为 −5.545 ns，原生为 +0.042 ns。即使后续证明零延迟逻辑等价，AMF3 仍未满足当前 8 ns 约束，不能保证按该时钟运行的硬件行为。时序不满足并不自动证明逻辑功能被编译流程改坏；这两个问题应分别验收。

要取得可信的后续功能结论，需要以共同输入 DCP 为参考，分别验证 AMF3 与原生结果。对明文计算核心，应保留相同边界协议、时钟、复位和初始状态，进行能处理寄存器复制及 DSP 寄存器外移的时序等价检查，或建立包含实际输入数据与期望结果的差分仿真。不能仅用复位后空跑或一直为零的输出作为有效测试。

完整 PCIe 系统可使用支持加密模型的 Vivado Simulator，配合 UNISIM/SECUREIP 和 PCIe/AXI 激励，比较 DMA 数据、AXI 控制访问及结果序列；加密不等于不能仿真。PCIe 和 GT 模型属于 SECUREIP 范围，见 [AMD 仿真库说明](https://docs.amd.com/r/2024.1-English/ug900-vivado-logic-simulation/Using-AMD-Simulation-Libraries)。有限激励的差分仿真仍不能代替对所有输入序列的形式证明。

## 复现与证据位置

服务器诊断目录：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261003-minimap2-functional-audit-172135-139332
```

其中保存三个 `functional.v`、全部差异 JSONL、输入哈希、源码扫描、原始日志和冻结脚本。原 DCP 未修改，未重跑布局布线。首次解析因未处理自动附加的 `glbl` 模板而拒绝继续；失败日志保留，修复版通过完整解析，比较耗时 54.70 秒。三个 Vivado 导出分别耗时 95.82、116.86、110.47 秒。

本地轻量报告位于 `local-reports/20261003-minimap2-functional-audit-172135-139332/`，只同步约 2 MB 的报告与清单，没有下载 DCP 或大型功能网表。

复用脚本：`scripts/diagnostics/run_minimap2_functional_audit.py`、`export_minimap2_functional_netlist.tcl`、`compare_minimap2_functional_netlists.py`。导出器先生成三个网表，再运行比较器；比较器不是通用 Verilog 前端或形式验证工具。5 项针对性测试覆盖真实参数/连接变化、物理属性和命名关联顺序、未知行为语法拒绝、`glbl` 独立哈希。
