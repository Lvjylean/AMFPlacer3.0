# GETRF 外部 floorplan：10ns / Vivado 2026.1 完整实验验收

实验已完成，完整布局、导入、布线和合法性检查通过；10ns setup 未收敛。没有流程失败或重试，本轮未修改运行算法源码。

## 结果

| 项目 | 结果 |
| --- | --- |
| Setup WNS / TNS | -0.436 / -2.557 ns |
| Setup 违例端点 | 16 / 607963 |
| Hold WHS / THS / 违例端点 | 0.023 ns / 0.000 ns / 0 |
| Pulse width 最差裕量 / 违例端点 | 4.300 ns / 0 |
| 可布线网络 / 完整布通 | 929,087 / 929,087 |
| 路由错误 | 0 |
| DRC Error / Critical Warning | 0 / 0 |
| AMF 真实单元导出覆盖 | 856,998 / 856,998，100% |
| 导入、place 后、route 后 LOC/BEL 保留 | 均为 856,998 / 856,998，100% |
| CARRY/DSP 等专用级联 | 检查 12,183，违规 0 |
| SRL 级联 | 检查 325，违规 0 |
| AMF 最终加权 HPWL | 3500027.621919 |
| Vivado 实际 SLL 使用量 | 11,840 |

导入策略为 repair，导入拒绝事件为 0；虽然本次全部位置被保留，但未运行 strict 策略，不能将 strict_import_verified=false 改报为严格导入验收通过。SLL 数来自最终 utilization.rpt 的 Total SLLs Used，表示实际资源使用量，不是唯一跨 SLR 网络数。

## 外部 floorplan 的作用与迁移

保留 InitialPacker、宏结构及相对偏移，以 15 个外部分区的 CR 集合初始化位置，跳过内部聚类和 SA。没有永久 CR/SLR 吸引项、Pblock 或区域锁定；后续 AMF 全局布局和打包可移动单元。原有 AMF→Vivado 位置交接策略保持不变。

初始建议区域内有 856,127 个单元（99.8984%）；最终为 608,580（71.0130%）。最终 248,418 个单元（28.9870%）位于本分区建议 CR 集合之外，其中 102,404 个（11.9492%）位于建议 SLR 集合之外。这是允许的移动，不是区域合法性违规。该比较是两阶段区域内数量，不把差值误称为逐单元移动总数。

最终分布：SLR0 407,867；SLR1 448,821；SLR2 310；SLR3 0。S14 的 6,933 个单元全部从初始建议 SLR2 改放到 SLR1；S13 最终无单元留在其建议 CR。由于导入后位置均被保留，这些区域调整发生于 AMF 后续布局/打包阶段。

| 分区 | 单元数 | 初始化区域内 | 最终区域内 | 最终建议 SLR 外 |
| --- | ---: | ---: | ---: | ---: |
| S00 | 21,622 | 21,619 | 12,639 | 8,983 |
| S01 | 504,505 | 503,868 | 423,633 | 80,872 |
| S02 | 40,892 | 40,851 | 11,858 | 57 |
| S03 | 46,480 | 46,435 | 35,222 | 1 |
| S04 | 37,961 | 37,943 | 21,946 | 175 |
| S05 | 52,597 | 52,571 | 28,298 | 130 |
| S06 | 43,041 | 43,030 | 33,817 | 0 |
| S07 | 49,374 | 49,347 | 27,550 | 0 |
| S08 | 15,923 | 15,905 | 1,669 | 205 |
| S09 | 3,964 | 3,960 | 11 | 0 |
| S10 | 10,943 | 10,925 | 9,332 | 59 |
| S11 | 8,945 | 8,928 | 30 | 48 |
| S12 | 7,892 | 7,886 | 2,575 | 0 |
| S13 | 5,926 | 5,926 | 0 | 4,941 |
| S14 | 6,933 | 6,933 | 0 | 6,933 |

统计覆盖全部 856,998 个真实成员，无缺失单元或未知 site。使用设备导出的 site→CR/SLR 元数据和 CR 集合并集，保留不连续区域的空洞；排除 77,777 个非成员原语（含常量/工具对象），不将其算成遗漏的 AMF 单元。

全设计数据连接几何审计：2,664,613 条 driver–sink pin 边，累计穿越 SLR 接缝 155,522 次、内部 HPIO 带 153,399 次；任一物理边界跨越边 240,366 条。这是端点位置的几何代理，不能代替实际 SLL 数。导入、place 和 route 三阶段相同。

## 耗时

| 计时范围 | 秒 | 分钟 |
| --- | ---: | ---: |
| AMF 进程（包含加载、打包、诊断和导出） | 2802.697 | 46.712 |
| 外部 floorplan 初始化日志区间（嵌于 AMF 进程） | 3.967 | 0.066 |
| 可证实布局执行日志区间（嵌于 AMF，含中间快照/诊断 I/O） | 2742.747 | 45.712 |
| AMF→Vivado Tcl 导出日志区间（嵌于 AMF） | 0.538 | 0.009 |
| AMF→Vivado Python 格式适配 | 38.691 | 0.645 |
| Vivado 进程（包含以下各阶段及其他开销） | 3479.763 | 57.996 |
| Vivado 打开 DCP | 78.784 | 1.313 |
| Vivado 导入 AMF 位置 | 638.128 | 10.635 |
| 导入审计 | 22.065 | 0.368 |
| Vivado place_design | 251.977 | 4.200 |
| 布局审计 | 23.341 | 0.389 |
| 写 placed DCP | 85.792 | 1.430 |
| Vivado route_design | 2107.270 | 35.121 |
| 路由后审计 | 24.246 | 0.404 |
| DRC/时序/资源报告 | 143.552 | 2.393 |
| 写最终 routed DCP | 51.675 | 0.861 |
| 关键路径诊断采样 | 1.469 | 0.024 |
| 完整实验总墙钟（manifest started→finished） | 6356.322 | 105.939 |
| 未包含在 AMF/适配/Vivado 三阶段中的入口及后处理差额 | 35.172 | 0.586 |

布局日志区间为 GlobalPlacement_fixedCLB started（34.774s）至开始最终 CLB 打包结果导出（2777.521s）。它含迭代中的快照/诊断 I/O，不是纯算法时间；不能仅减掉最后 Tcl 导出就声称获得纯算法时间。嵌套计时不能重复相加。DCP→AMF 输入缓存复用，本轮未执行输入格式导出。只读版本/时钟预检及本报告的后续审计不计入完整运行 manifest 的总墙钟。共享服务器上的耗时不能直接解释为 floorplan 加速。

## 约束与结论范围

AMF ClockPeriod=10；Vivado 最终 ap_clk=10.000ns，波形 {0.000 5.000}。Vivado 2026.1，AMF 8 线程、Vivado 4 线程。输入和器件与预检哈希一致，初始 DCP 的 Pblock 数为 0。

原始设计是 OOC：HD.CLK_SRC 未指定；124 个输入和 250 个输出缺少 delay 约束；无时钟寄存器和未约束内部端点均为 0。DRC 对 OOC 的检查范围也有提示。因此这是当前 OOC 约束下的结果，不是板级完整接口时序签核。

最差 setup 路径数据延迟 10.414ns，其中逻辑 0.760ns、布线 9.654ns（92.702%）。本轮验证了外部初始化能够贯通完整流程，以及 AMF 确实可以离开建议区域；没有同版本、同冻结二进制的无 floorplan 对照，不能声称改善或恶化由 floorplan 单独导致。负 WNS 是本轮 QoR 结果，不是应通过修改验收条件掩盖的流程故障。

已有 timing_sample_analysis 属于 cell-site 近似诊断，使用的简化距离函数不是本轮原生已校准延迟估计器；该文件不作为本轮拟合准确度结论。

## 追溯与产物

- 运行：`getrf-external-floorplan-10ns-vivado2026-1-full-20261007-013540-091549`
- 副本：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo`
- 实现提交：`bbfbd584f5991928d4eea98f9c7073cfcadc22df`
- 构建：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/builds/build-20261007-011234-837379-13761ef9/build/AMFPlacer`
- 二进制 SHA-256：`4aafbd2ed48194b878704441bc436ccedfa3c7f1033d19725b9325a0589fc0d4`
- 输入 DCP SHA-256：`6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`
- 最终 DCP（仅服务器）：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/experiments/runs/getrf-external-floorplan-10ns-vivado2026-1-full-20261007-013540-091549/reports/getrf_routed.dcp`
- 最终 DCP SHA-256：`6c7abb432efcf33f7585e73a92484e27e275d2c0da48ec8e05a7874246011227`
- 验收/区域/原码保护证据：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/experiments/evidence/external-floorplan-10ns-vivado2026-1-20261007-013306`

构建使用先前副本基线提交及完整源码快照；已有逐文件核验确认其 src 与实现提交一致。原项目基准的 3,634 个源码/脚本/配置/测试文件再次逐一核对，无变化。新增结果审计脚本只写入副本，经过不连续 CR 区域空洞、跨 SLR、常量排除和未知 site 拒绝验证；未改动本次运行加载的源码/脚本。
