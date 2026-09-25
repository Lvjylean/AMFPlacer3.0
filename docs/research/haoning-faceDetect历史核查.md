# haoning 的 faceDetect 历史实验核查

核查日期：2026-09-25。只读检查 eda072 上 `/Projects/haoning` 的可读项目目录。

## 结论

该目录保存了 faceDetect 的 AMFPlacer 布局成功记录，以及 AMF 布局导入 Vivado 2024.2 后成功布线的记录。历史 AMF 日志使用预先导出的 benchmark 宏文件；没有证据表明那次成功运行包含我们本次执行的“从 DCP 重新导出全部宏输入”步骤。

共检查相关 87 份日志，没有发现本次 `loadOtherCLBMacros()` / `cellInMacros.find(tmpCell)` 重复宏归属断言。这个结论只覆盖当前保留且可读的日志，不能证明 haoning 从未遇到过该问题。

## 成功运行证据

`/Projects/haoning/AMF_Results/AMF_origin_rerun/faceDetect/run.log`：

- 第 33 行配置：`unpredictable macro file = ../benchmarks/VCU108/design/faceDetect/faceDetect_unpredictableMacros`。
- 第 126–127 行：`#CLB Macro: 12`、`#CLB Macro Cells: 53`。
- 第 11118 行：`Placement Done (elapsed time: 415.942 s)`。

`/Projects/haoning/AMF_Results/AMF_origin_vivado2024/faceDetect_vivado.log`：

- 会话日期：2026-03-29，18:08:25 至 18:33:00；Vivado 2024.2。
- 第 19 行工程：`/Projects/haoning/Vivado_Results/faceDetect_project/VCU108_PCIE_faceDetection/VCU108_PCIE_faceDetection.xpr`。
- 第 21 行导入布局：`/Projects/haoning/AMF-Placer/build/dumpData_faceDetect/DumpCLBPacking-first-0.tcl`。
- 第 274899 行：`route_design completed successfully`。
- 第 274927 行：`RESULT: WNS=0.299 TNS=0.000`。

同目录 `faceDetect_timing.rpt` 第 10 行注明 `Design State: Routed`。这些证据说明流程完成和对应时序结果，不代表没有任何 methodology、IO 约束或板级问题。两份不同目录的 AMF 与 Vivado 日志不能直接认作同一条流水线后相加计时。

## 为什么历史成功记录未触发本次问题

当前保留的历史宏文件路径：

```text
/Projects/haoning/AMF-Placer/benchmarks/VCU108/design/faceDetect/faceDetect_unpredictableMacros
```

该文件有 54 条记录，都是 LUTRAM（45 个 RAM32X1D、9 个 RAM64X1S），与本次实验保存的 `inputs/baseline/faceDetect_unpredictableMacros` 逐字节相同，SHA-256 为 `47bc88ae1abbbd9dc2ed3a25e881cd7fb9f2728141adbdc5eade853b5e261d7a`。

历史日志仅加载 53 个 CLB 宏单元，与加载器无条件跳过第一行的行为吻合。历史文件不包含本次冲突的 52 个 KEEP 寄存器；新导出文件则有 1,179 条记录、797 个不同单元，包含这些寄存器。

haoning 当前的 `extractLUTRAMs.tcl` 和 `InitialPacker.cc` 与 jinyang 对应文件逐字节相同：导出脚本第 67 行仍选择 `KEEP =~ "*yes*"`；加载器第 1531 行仍直接断言单元没有已有宏归属。因此没有发现他已经在这两处代码中修复本次问题的证据。历史成功与当前失败的已证实差别在于实际宏输入，而非这两份当前代码。

## 日志检索范围

- `/Projects/haoning/AMF_Results`：77 份 `.log/.out/.err`，不扫描复制工程的 RTL 树。
- `/Projects/haoning/AMF-Placer/build` 与 `/Projects/haoning/amf_routing`：另 10 份日志。
- 相同宏加载断言：未发现。
- 另发现 `/Projects/haoning/AMF_Results/AMF_TimingDriven/digitRecognition/run.log:4445` 存在 `PlacementInfo.cc:972` 的 `inRange(cellX, cellY)` 断言，发生于另一个 case 的网格范围检查，不是本次宏重复归属问题。

本次未读取不可访问的用户 home，也未把缺失或被删除的历史当作不存在。
