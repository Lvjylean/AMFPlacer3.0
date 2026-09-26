# U250 / GETRF 移植范围与输入预检

日期：2026-09-26。本文记录开始实施前的范围确认和只读输入检查。用户随后授权逐步实施，输入阶段见 [U250 输入适配](u250-input-adaptation.md)，2026-09-26 完整布局布线基线见 [全流程报告](u250-getrf-full-flow.md)，2026-09-27 纵向边界代价见 [SLR 时序修正](u250-slr-timing-penalty.md)。

## 当前用户决定

- 修改 AMFplacer3.0，使其能够处理 U250，目标器件 `xcu250-figd2104-2L-e`。
- case 使用 GETRF 的原始 post-opt DCP，暂不使用用户独立项目的 partition、floorplan、membership、软 Pblock 或后续控制路径修整。
- 多 SLR 使用统一二维布局坐标。2026-09-27 用户进一步要求在现有延迟模型上加入较大的纵向 SLR 边界惩罚；仍暂不考虑 SLL 容量与估计拥塞。
- 需要真实 U250 器件库、URAM 读取和放置支持，以及专用级联的物理合法性检查。

这调整了此前跨项目接入评估中的实施顺序，但不否定独立前端已有能力，也不代表完整多芯粒优化或增量编译器已经完成。

## 已核对的原始输入

服务器 `eda070`：

```text
/Projects/jinyang/workspace/non_dataflow_crossSLR/case/experiment/getrf_partition_floorplan_20260923_7VSNOFzw/optimized/post_opt.dcp
```

SHA-256：`6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`。后续实施已将 DCP 流式复制到 eda072 的 `data/reference/getrf-u250/post_opt.dcp` 并重新计算哈希，确认一致；没有下载 DCP 到本地。

- metadata 指定 part：`xcu250-figd2104-2L-e`。
- 原始时钟报告为 `ap_clk`，周期 10 ns。这是输入中的已有约束记录，后续 AMF 目标配置仍以用户给 placer 的约束为准。
- 原始利用率报告：256 个 URAM、2816 个 DSP48E2、515 个 RAMB36E2、1 个 RAMB18E2。
- 原始 `raw/cells.tsv` 中共 912,247 cells，包括常量单元；本轮逐行检查原语类型与 `raw/nets.tsv` 的 driver/sink 端口，没有使用 floorplan 结果。

## 专用连接检查结果

以源/目标原语类型和连接端口识别专用级联候选：

| 资源 | 相关 cells 总数 | 专用连接的不同 cell 对数量 |
|---|---:|---:|
| CARRY8 | 22,508 | 10,939 |
| DSP48E2 | 2,816 | 1,280 |
| URAM288 | 256 | 0 |
| RAMB36E2 / RAMB18E2 | 515 / 1 | 0 |

DSP 专用 sink 端口检查包括 ACIN、BCIN、PCIN、CARRYCASCIN 和 MULTSIGNIN；总计 92,160 条 pin 级连接，不等于 92,160 条独立链。Carry 检查 CARRY8 到 CARRY8 的 CI 连接。URAM 检查 CAS_OUT 到 CAS_IN，BRAM 检查 RAMB 之间的 CAS 端口连接。未发现 URAM 的 CAS 输出连接到原始导出中的 sink。

结果说明：当前 case 的 256 个 URAM 可以先按独立资源块支持；不能据此声称已支持其他设计的 URAM cascade。GETRF 仍有 Carry 和 DSP 专用连接，移植不能忽略相应 SLR 边界合法性。上述统计来自已有原始导出，不代替 Vivado 对最终位置的验证。

## 为什么专用级联不能跨 SLR

SLR 内部专用的 Carry、DSP、BRAM 和 URAM 级联线路在芯粒边界处不连续。普通信号可以经可编程互连和 SLL 跨越边界；使用专用级联端口的连接则需要对应的专用物理通路。对此进行改接可能涉及原语配置或网表变换，不属于本次仅修改 placer 的范围。

因此准确规则是：使用专用级联的链必须放在满足专用通路连接关系的站点上，处于同一 SLR 是必要条件之一；普通逻辑群和通过普通数据端口连接的多个独立 RAM/DSP 不受这条同 SLR 规则约束。

AMD 官方依据：[UG949 Propagation Limitations](https://docs.amd.com/r/en-US/ug949-vivado-design-methodology/Propagation-Limitations)、[UG573 UltraRAM Cascade](https://docs.amd.com/r/en-US/ug573-ultrascale-memory-resources/UltraRAM-Cascade)。

AMF 源码中已经存在相应物理宏概念：`src/lib/HiFPlacer/placement/packing/InitialPacker.cc:203` 的 DSP 宏识别读取 ACIN/BCIN/PCIN/CARRYCASCIN；`:396` 的 BRAM 宏识别读取 CASDI。后续移植还需检查识别完整性、候选站点的连接连续性及 SLR 归属，而非直接套用“所有物理宏不得跨 SLR”。
