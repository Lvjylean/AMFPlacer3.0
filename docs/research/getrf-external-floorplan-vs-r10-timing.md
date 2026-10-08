# 外部 floorplan 与 R10 无外部 floorplan 的时序比较

现有最接近同条件的历史对照显示，本次接入外部 floorplan 后 setup 变差：WNS 从 +0.080ns 降到 −0.436ns，下降 0.516ns（516ps），从满足 10ns 变为 16 个 setup 端点违例。Hold 两轮均通过。

| 指标 | 无外部 floorplan（R10） | 外部 floorplan | 后者−前者 |
| --- | ---: | ---: | ---: |
| WNS / ns | +0.080 | −0.436 | −0.516 |
| TNS / ns | 0.000 | −2.557 | −2.557 |
| Setup 违例端点 | 0 | 16 | +16 |
| WHS / ns | +0.020 | +0.023 | +0.003 |
| Hold 违例端点 | 0 | 0 | 0 |
| AMF 最终加权 HPWL | 3190300.817061 | 3500027.621919 | +9.71% |
| 实际 SLL 使用量 | 10,070 | 11,840 | +17.58% |

两轮 setup 总端点均为 607,963；完整布通，DRC Error/Critical Warning 均为 0/0，AMF 原始 LOC/BEL 在后端保留率均为 100%。

## 同条件核验

同一原始 GETRF post-opt DCP，SHA-256 为 6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031。共同输入的 9 项哈希全部一致，包括网表、时钟、器件坐标、物理边界模型、资源映射和时钟容量数据。常规算法参数相同：ClockPeriod=10、y2xRatio=0.4、GlobalPlacementIteration=30、BoundaryAwareClustering=true、SLRBoundaryDelayNs=1.5、legacy matching 等；外部版本新增 membership/regions/manifest/report 配置和 ExternalFloorplanReferenceClusterCount=54。资源映射路径搬到副本，但内容相同。

两轮均使用同一 Vivado 2026.1 build 6511674；AMF 8 线程、Vivado 4 线程；repair 导入策略相同；full_backend.tcl、import_acceptance.tcl 和边界诊断 Tcl 哈希一致。

冻结源码共同文件有 3413 个一致，差异仅在外部初始化入口、初始化检查入口、新 ExternalFloorplan 文件及相关构建/测试文件；延迟系数、STA、全局优化器、打包器与合法化器源码未额外改变。

仍有必须保留的差异：R10 开启运行计时插桩，本轮关闭；二进制不同，每种条件只有一次完整运行，没有重复试验或严格控制并行执行差异。因此，这是可信的历史近似同条件对照，不能称为同一冻结二进制上的严格单变量因果结论。

## 如何解释

本轮没有观察到外部 floorplan 的时序收益。总 HPWL 和实际 SLL 用量同时上升，与连线/跨 SLR 压力加大的方向一致，但不能仅靠这两个总量确定最差路径变差的原因。

两轮最差路径不同：R10 为 FDRE→RAMB36E2，数据路径延迟 9.582ns；外部版本为 subUpdate 浮点减法内部 FDRE→FDRE，7 级逻辑，数据路径延迟 10.414ns（逻辑 0.760ns、布线 9.654ns）。因此不能将 WNS 差值直接解释为同一条路径增加了 0.516ns 连线延迟，也不能用不同最差路径的分项相减做因果分解。

两轮均为 OOC 结果，存在 HD.CLK_SRC 缺失及部分接口 delay 约束缺口；该比较仍限于当前 OOC 约束范围。确认外部初始化具备可运行性和放置自由度，不等于本次 floorplan 更适合后续 AMF 优化。

## 可追溯记录

- 无外部 floorplan：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-r10-vivado2026-1-10ns-full-20261006-132647-570065
- 外部 floorplan：/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/experiments/runs/getrf-external-floorplan-10ns-vivado2026-1-full-20261007-013540-091549
- 结构化比较：/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261007-external-floorplan-copy/repo/experiments/evidence/external-floorplan-r10-comparison/comparison.json
- 本次只读取既有实验并保存比较报告，未启动新实验。

## AMF 进程耗时补充

| 计时范围 | R10 无外部 floorplan | 外部 floorplan |
| --- | ---: | ---: |
| AMF 进程墙钟 | 2222.076s（37.035min） | 2802.697s（46.712min） |
| Cluster Placement Start→Done | 24.972s | 3.967s |
| SA 日志区间 | 0.654s | 跳过 |
| 后续布局/打包日志区间 | 2141.444s（35.691min） | 2742.747s（45.712min） |

本次 AMF 进程没有缩短，而是增加 580.620s（9.677min，26.130%）。外部初始化省去 21.005s，但后续区间增加 601.303s，抵消了初始化收益。后续区间两轮均从首次 GlobalPlacement_fixedCLB started 到开始最终 CLB 结果导出，含中间诊断和快照 I/O，不是纯算法计时。

R10 的 SA 本身只有 0.654s；被替代的整个内部聚类/SA 区间约 25s，只占原 AMF 进程约 1.12%。进一步读取逐次匹配日志后，已定位到 SLICEM 宏匹配求解累计增加 593.533s；完整宏合法化区间增加 593.861s，最终打包/细化区间反而减少 6.880s。两轮全局 CLBElements 都是 38 轮。详见 [完整时间分解与原因分析](getrf-external-floorplan-runtime-breakdown.md)。计时插桩、二进制和共享主机负载差异仍然存在；不能将一次运行的差值扩大为普遍减速或严格因果结论。
