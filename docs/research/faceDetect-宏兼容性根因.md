# faceDetect 宏兼容性根因

核查日期：2026-09-25。对象为 eda072 上 `faceDetect-20260925-131037` 首轮失败实验。

## 结论

宏导出脚本把 `KEEP=yes` 的普通流水线寄存器也导出为按原 SLICE 分组的 CLB 宏。其中 52 个 FDRE 已被 AMF 的 `findCARRYMacros()` 纳入进位链宏。后续 `loadOtherCLBMacros()` 再次收编它们，违反一个单元只能属于一个宏的假设，在 `InitialPacker.cc:1531` 触发断言。

这是导出器与宏加载器之间的规则冲突。已通过原二进制运行时检查和两组对照运行确认；没有证据将本次断言归因于 Vivado 版本差异。

## 精确触发路径

服务器源码及脚本位置：

1. `/Projects/jinyang/AMF-Placer/benchmarks/vivadoScripts/extractLUTRAMs.tcl:67`：`get_cells -hierarchical -filter { KEEP =~ "*yes*" }` 选择所有符合条件的单元，将名称、LOC、BEL 加入 `unpredictableMacros`。
2. `/Projects/jinyang/AMF-Placer/src/lib/HiFPlacer/placement/packing/InitialPacker.cc:25`：先执行 `findCARRYMacros()`；其约 1033–1087 行沿 CARRY8 输出查找兼容寄存器，并吸收入进位链宏。
3. 同文件第 32 行：再执行 `loadOtherCLBMacros()`；其约 1452–1465 行按输入中的 LOC 分组，每个站点建立 CLB 宏。
4. 同文件第 1531 行：`assert(cellInMacros.find(tmpCell) == cellInMacros.end())` 检测到重复归属并退出。

上述加载顺序及实际冲突用运行目录内保存的原始 AMFPlacer 二进制进行了复现，没有重编译或绕过断言。

首个断言单元（cell ID 6029）：

```text
design_1_i/face_detect_0/inst/face_detect_fdiv_4jc_U141/face_detect_ap_fdiv_3_no_dsp_32_u/U0/i_synth/DIV_OP.SPD.OP/MANT_DIV/RT[12].ADDSUB/ADDSUB/Q_DEL/i_pipe/opt_has_pipe.first_q_reg[0]
```

其 D 端由同一 RT[12] 加减器的 `CHAIN_GEN[0].C_MUX.CARRY_MUX_CARRY4_CARRY8/O[0]` 驱动，原位置为 `SLICE_X71Y24`。它在导入 CLB 宏前已经属于以以下单元命名的进位链宏：

```text
design_1_i/face_detect_0/inst/face_detect_fdiv_4jc_U141/face_detect_ap_fdiv_3_no_dsp_32_u/U0/i_synth/DIV_OP.SPD.OP/MANT_DIV/RT[0].ADDSUB/ADDSUB/CHAIN_GEN[0].C_MUX.CARRY_MUX_CARRY4_CARRY8
```

共确认 52 个冲突单元，涉及浮点除法器实例 U141、U142 中的四个进位链宏。直接打开原 DCP 查询，52 个单元属性全部为 `REF_NAME=FDRE`、`KEEP=yes`，`ASYNC_REG` 和 `XPM_CDC` 均为空。因此本次直接触发冲突的是 KEEP 分支，不能把故障笼统归咎于同步器约束。

## 对照验证

| 输入 | 宏文件记录数 | 结果 |
|---|---:|---|
| 首轮原始导出 | 1179 | 原断言失败 |
| 仅删除完全相同的重复记录 | 797 | 同一断言失败 |
| 在去重基础上，仅从外部宏文件排除 52 个已有宏归属的单元 | 745 | CLB 宏加载通过，已到达 `findLUTRAMMacros()` |

382 个重复记录均具有相同的 LOC/BEL，加载器在每个站点内用集合去重；它们不是本次断言的直接原因。历史文件只包含 54 个 LUTRAM 单元，不含这些普通寄存器，因此没有触发这类交叉归属。

所有对照运行均在调试器断点处结束，没有继续执行全局布局和布线。“通过宏加载”只说明本次断言原因得到验证，不代表完整流程已跑通。排除 52 个单元只发生在独立诊断输入，不是正式修复；原实验配置、源码和原始文件均保留。

## 修复方向

应修正宏提取规则：不能仅凭 `KEEP` 属性就把普通寄存器视为必须按原 SLICE 绑定的物理宏。KEEP 用于保留信号、控制优化，并不表达这样的放置要求；相关语义见 [AMD UG901：KEEP](https://docs.amd.com/r/en-US/ug901-vivado-synthesis/KEEP)。

LUTRAM 结构约束和真正的同步器放置约束仍需保留。ASYNC_REG 确实会影响同步链寄存器的相邻放置，见 [AMD UG912 2024.2：ASYNC_REG](https://docs.amd.com/r/2024.2-English/ug912-vivado-properties/ASYNC_REG)。加载器还应在创建新宏前检查已有归属，明确处理冲突并输出单元名、已有宏和新宏，避免只有断言信息。

同时发现加载器第 1448 行无条件跳过宏文件首行，而当前导出文件没有表头。这是另一个输入格式问题，会漏读首个单元，不是本次 52 个寄存器冲突的直接原因，正式修复时应一并处理。

## 证据

远端完整诊断目录：

```text
/Projects/jinyang/workspace/amf-runs/faceDetect-20260925-131037/diagnostics/macro-conflict-134719
```

本地证据：[诊断汇总](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/diagnostics/faceDetect-macros/diagnosis.json)、[52 个冲突单元及已有宏](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/diagnostics/faceDetect-macros/native-macro-conflicts.json)、[DCP 属性表](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/diagnostics/faceDetect-macros/cell-properties.tsv)。

后续检查 haoning 历史记录确认：其 faceDetect 使用预先导出的 LUTRAM 宏文件，存在 AMF 布局和 Vivado 布线成功记录；87 份相关日志中未找到同一宏归属断言。见[历史核查](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/haoning-faceDetect历史核查.md)。
