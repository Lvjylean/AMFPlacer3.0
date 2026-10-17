# GETRF AMF 输入准备优化（2026-10-14）

## 原因与范围

本次已定位的长耗时发生在 `full_amf_input`：AMF 尚未启动，Vivado 正将完整设计导出成 AMF 输入。不能把它算作 AMF placement 时间。

旧脚本对每个 cell 重新查询 cell/pin，对每个 pin 单独查询 net、类型、方向、时钟属性，并按不同层次网名反复查询同一逻辑连接的驱动。GETRF 有约 85.5 万 fabric primitives，层次与硬原语多，串行 Tcl/Vivado 查询数量过大。进程仍消耗 CPU，没有证据表明是等待许可证或死锁。主要依据是源码调用路径、持续增长的导出计数及 CPU 记录；没有把未经采样的函数耗时捏造为精确占比。

旧任务停止时的实测：

| 服务器／版本 | 导出已耗时（约） | 最后进度日志 | 完成比例 |
|---|---:|---:|---:|
| eda072／V0 | 38.12 min | 155,648 / 855,092 | 18.20% |
| eda099／V1 | 40.78 min | 172,032 / 855,088 | 20.12% |

时间起点采用已保存 `base_opt.dcp` 的修改时间，近似导出开始，包含最初清单查询；进度是最后一次定期日志，不是最后一个已写 cell。旧导出被主动停止，不能据此报告其完整耗时或精确总加速倍数。

## 优化实现

1. 每批 256 个 canonical primitive，批量读取 pin 名称、参考端口、方向、时钟和连接标志。
2. 每个逻辑 net 的层次连接只遍历一次；使用 Tcl array 缓存 pin、驱动、sink 数和别名。
3. 保留原来的 cell/pin 顺序及每个 pin 的本地层次网名。硬原语内部 Unisim segment 不当作外部连接；不使用会展开 DSP/RAM 内部逻辑的 `-leaf` 查询。
4. 保留常量、时钟、DSP 注册输出认证、SRL 必要属性、静态／RP 完整资源掩码和覆盖检查。额外检查已连接 pin 不得漏导、未连接 pin 不得误导、普通信号必须恰有一个 canonical driver。
5. 分开记录批量 pin 信息、逻辑连接遍历、文件／元数据写出，以及 Python 压缩、输入哈希和等价检查时间。

本次未改变 AMF 算法、布局约束、线程数、时钟或延迟参数。二进制保持 `e097c317b7fff66e3ddd1a5291c86d1a49f7b1bfa589f96d181c9be73217df90`。

## 验证与恢复

- 072 的 CARRY/RAM/SRL 小型 DFX，以及 099 含 DSP 的小型 DFX，各 10 份输入文件新旧逐字节一致：netlist、cells、clocks、DSP 输出认证、SRL 属性、专用接口、基础设施、区域、区域 site 表和覆盖统计。
- 099 另以每批 8 cells 验证跨批次的重复别名／常量处理，10 份文件仍逐字节一致。
- 大 GETRF 在启动 AMF 前逐行比较旧导出中所有完整 cell；中断时的最后一个不完整 cell 排除。该检查是完整前缀的等价证据，不冒称旧版完整 GETRF 导出已经完成。
- 两个旧任务的进程身份、原 manifest/status、最后进度及 DCP 哈希均存档。只停止本实验的进程组，已有 GUI 和其他任务不动。
- 新任务复用旧 `base_opt.dcp`，不重新综合或 `opt_design`。099 使用新的 `experiments/deployments/full-amf-input-batch-02` 脚本快照，原部署保留。

## 当前正式实验

根目录分别为：

- eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo`
- eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo`

| 服务器 | 相对运行目录 | 工作 |
|---|---|---|
| 072 | `experiments/runs/getrf-full-amf-contained-v0-072-02` | 全量 V0 AMF + Vivado，生成 static shell |
| 072 | `experiments/runs/getrf-full-amf-contained-incremental-072-02` | V0 验收通过后并行 A/B 增量，再拼接完整 V1 |
| 099 | `experiments/runs/getrf-full-amf-contained-v1-099-02` | 同一 V1 的全量 AMF + Vivado 对照 |

启动约为 04:00 HKT。截至约 04:25，两组均完成全部输入准备，已实际进入 AMF 全局布局；还没有本轮 placement/routing 最终结果。

| 实测步骤 | 072／V0 | 099／V1 |
|---|---:|---:|
| 重新读取已保存 opt DCP | 104.793 s | 86.550 s |
| 区域、cell、pin 信息读取 | 290.964 s | 238.163 s |
| 逻辑连接遍历 | 682.956 s | 585.011 s |
| 网表／元数据写出 | 146.393 s | 131.036 s |
| 导出函数收尾（总计减上述三项） | 15.377 s | 13.747 s |
| **完整网表导出** | **1135.690 s（18 min 55.690 s）** | **967.957 s（16 min 7.957 s）** |
| Vivado prepare 进程总墙钟 | 1308.686 s | 1106.086 s |
| 旧导出前缀核对 | 1.506 s | 1.363 s |
| ZIP 压缩 | 16.280 s | 14.521 s |
| 输入哈希 | 0.085 s | 0.062 s |

Vivado prepare 进程还包括启动、DCP 读取、恢复区域校验和退出；它与网表导出不是同一口径，也不是 AMF placement。两台不同服务器、V0/V1 四个 cell 的差别都如实保留，不将两列相差解释为代码加速。

大网表等价门禁已通过：072 的 156,906 个完整 cell、1,225,999 行，以及 099 的 174,586 个完整 cell、1,374,047 行，和旧脚本保存的部分逐字节一致；区域、site 掩码和基础设施表也一致。旧导出未完成，故不宣称完成了两套完整 GETRF 文件之间的比较。候选输出中的所有 fabric pin 连接完整性／单驱动检查通过，AMF 实际读取 cell 数分别为 855,092／855,088。

## 下一项输入优化验证（不重启正式 AMF）

实测连接遍历仍占导出的大部分时间。当前批量版本会遍历约 490 万层次网段，其中很多仅通向内部层次，没有直接连接 canonical primitive pin。

099 的只读探针确认：批量 `get_nets -of_objects` 可保留每个 pin 的直接网段，而 `PARENT` 提供共同逻辑 net 的代表。因此建立第二个独立候选，直接遍历所需网段并按 PARENT 汇总驱动和 sink，避免无关层次扩展。小型含 DSP/RAM/CARRY/SRL case 的 10 个文件已逐字节一致。

大网表候选验证在 099 `experiments/preflight/full-export-direct-getrf-01`，runner PID 1201134，约 04:23 启动，启动前 MemAvailable 约 90.7 GiB。仅重新导出已保存 DCP，不做 placement、routing，也不更改正式运行。完成后自动对全部 10 个输出文件做 SHA-256 比较；只有全文件等价且实测更快，才能采用为后续运行的默认导出器。已有正式 AMF 不为此重跑。该候选尚待验证，不把预计提速当成结果。

10 分钟 heartbeat 已启用，索引为 072 的 `experiments/evidence/static-containment-01/launch.json`。无可行动变化时静默；发现实际异常先保存现场并定位，再修复、验证、从最近可用产物恢复。保持 AMF placement 与 Vivado routing 各自增量不超过匹配全量 V1 的 70% 的验收标准。Vivado 兼容性 placement 单列，DCP／bitstream 仅保留服务器。

详细证据：`experiments/evidence/static-containment-01/input-export-optimization-01/`，包含两端取消记录、脚本哈希、小型等价验证和阶段计时镜像。


## 04:38 HKT 第二版完整验证通过

直接网段导出器已在同一台 eda099、同一份 V1 opt DCP 上完成。10 个完整输出文件的 SHA-256 全部与第一版批量导出一致，涵盖完整网表、cell、时钟、DSP 注册输出、SRL 属性、接口、基础设施和区域／site 掩码。此项是两版批量导出器之间的全文件等价证据；与最早逐 pin 脚本的比较仍限于上述已保存前缀及小型 case，不扩大其证明范围。

| 099 V1 输入导出 | 第一版批量 | 第二版直接网段 |
|---|---:|---:|
| 区域、cell、pin | 238.163 s | 280.479 s |
| 逻辑连接 | 585.011 s | 321.253 s |
| 写出与元数据 | 131.036 s | 155.343 s |
| 函数收尾 | 13.747 s | 15.084 s |
| **完整导出** | **967.957 s** | **772.159 s** |

实测完整导出减少 195.798 s（20.23%），连接处理减少 45.09%。第二次诊断与正式 AMF 并行，后台负载未做严格控制，因此报告为本次实测，不保证每次等比例提速。该独立诊断 runner 总墙钟为 878.646 s，包含 Vivado 启动／读取 DCP／导出／退出及输出哈希；不是正式 prepare 流程的完全同口径计时。峰值 RSS 52,414,728 KiB，约 49.99 GiB；后续并发调度需计入这部分内存。

两台服务器均已冻结 `experiments/deployments/full-amf-input-direct-03/`，入口为其中 `scripts/run_dfx_full.py`；仅切换两个 full-input 准备分支的导出器，未改 AMF 二进制或增量边界时序输入。入口导入／命令行检查通过，部署 manifest 记录全部脚本哈希和验证来源。当前 V0/V1 正式运行保留第一版已完成的输入，不重新启动，也不改运行中的快照。

截至本次后续核查，两台 AMF 仍在全局布局中，日志和 CPU 持续推进，无错误退出；V0 完整后端验收前不会提前启动依赖其 static shell 的增量工作。最新逐次状态见 `experiments/evidence/static-containment-01/monitor/`；当前仍无本轮完整布线结果，70% 性能验收待完成。


## 05:06 HKT：AMF 完成，修复全量导入拆分 site 的错误

| 项目 | 072 V0 | 099 V1 |
|---|---:|---:|
| AMF 进程实测墙钟 | 1969.035 s（32 min 49.035 s） | 1498.189 s（24 min 58.189 s） |
| 退出码 | 0 | 0 |
| 导出 fabric 实例 | 855,092 / 855,092 | 855,088 / 855,088 |
| AMF 导出区域违例 | 0 | 0 |

以上是 AMF 进程口径，不冒称全部都是纯算法时间。两组均未重跑 AMF。

原全量 DFX 导入器按实例名排序、每 256 个实例调用一次 `place_cell`，可能将同一 site 的 LUT、F7/F8 MUX 和 FF 拆开。两端实际均在 `import_full_amf` 报 Vivado 12-1409，布局优化、布线都尚未开始。099 出错 site 为 SLICE_X122Y230，072 为 SLICE_X160Y356。原 AMF 打包导出将同 site 成员置于一个调用中。修复改为以完整 site 为分组边界，保留每个 cell 原来的 LOC/BEL，不释放约束、不忽略错误；同时修正 site-only RAM 目标的保留率比较口径。

独立回归逐条核验两端全部 855,092 / 855,088 个请求，覆盖无遗漏、无重复、目标无改变且同 site 不跨调用。新部署为两端 `experiments/deployments/full-amf-site-import-04`，恢复运行分别为：

- 072：`experiments/runs/getrf-full-amf-contained-v0-072-backend-03`
- 099：`experiments/runs/getrf-full-amf-contained-v1-099-backend-03`

新目录复用各自 opt DCP、已完成 AMF 布局和区域表，记录父 manifest、复用输入与脚本哈希。prepare/AMF 阶段标记 reused，保留原实测时间；新 run 的 wall_seconds 只代表此次后端恢复，不能当作包含 AMF 的全流程总墙钟。失败尝试与恢复开销另外报告。

截至 05:05:40，099 实际导入已经通过：151.331 s，855,088 cells / 100,623 sites / 3,422 次调用，区域审计通过；写出 `base_imported.dcp` 后已进入 Vivado placement 初始化。855,086 个实例精确保留 LOC/BEL；两个静态 LUTRAM 的 site 保持不变，但 BEL 分别从 H6LUT 读回 H5LUT / G6LUT，须保留此差异记录，不能声称 100% BEL 不变。072 仍在实际导入。两组尚无本轮完整布线结果。

072 原有限流水线在导入失败后已退出，增量任务没有提前启动。下一步须从恢复成功的 V0 生成的 locked shell 启动新的增量运行，配置 shell_run 指向恢复目录，不再次执行旧流水线或重跑全量 AMF。

## SRL 元数据语义审计与单独修正

原常规导出器区别“IS_CLK_INVERTED 属性不存在”与“属性存在但值为空”；DFX 导出器此前将二者均转换成 UNKNOWN。现有共享策略故意拒绝 UNKNOWN，却允许将原始空值作为独立兼容类别。因此本轮 SRLMaxPerSLICEM=4 虽已配置，共享实际上被保守关闭。这是早于批量加速的输入语义缺漏，不是两版批量导出之间的差异。

099 对实际 V1 opt DCP 的只读审计完成：12,261 个 SRL（11,597 SRL16E、664 SRLC32E）全部具有此属性，但原始值为空。V0 SRL 名称和类型逐项与此清单匹配。修正保留原始空值，不擅自等同于 0；只有属性不存在才保留 UNKNOWN。属性存在性按固定 primitive 类型缓存，避免恢复逐实例 list_property 的开销。8 个回归用例验证空值、显式 0/1、不存在属性与不支持的 primitive。

未来修正部署为两端 `experiments/deployments/full-amf-input-srl-05`，包含已完成全文件等价验证的直接网段导出器，但尚未用于正式运行。额外 C++ 兼容性回归在 072 `experiments/preflight/srl-sharing-semantic-probe-01`：使用冻结库、独立测试程序，只加载输入和 InitialPacker，再进行人工同位置兼容测试；不执行 GP 或 Vivado。它不能代表真实布局质量或时序验证。原 UNKNOWN 输入已按预期得到“无共享组”，修正输入检查进行中。

当前 V0/V1 和其后续配对增量保持相同的原始保守元数据条件；不能将未来修正后的增量与本轮未修正全量不加说明地比较。待独立修正通过后，完整验证需另设匹配条件的实验，现有正常后端继续运行。


05:08 补充：072 site 整体导入也已实际通过（188.104 s），855,092 个实例区域正确，855,090 个 LOC/BEL 精确保留，两个 LUTRAM 的 site 不变、BEL 读回值与请求不同（是否仅为 primitive 映射表达差异仍须确认）。两组均不再卡在此前 MUX 导入错误。

SRL 独立 C++ 回归已完成：原始 UNKNOWN 输入如预期没有共享组；修正输入通过，人工同位置兼容测试得到 3,035 个共享组，PU 数保持 580,450 不变，单组 SRL 数不超过 4；重建稳定性、最终打包器多 owner 存储及 100 个共享组的净线长代价核验通过。这里的共享数是人工同位置诊断，不是正式 GP 的共享数量，也不是已实测的合法化加速。

后续增量导入器同样发现逐 cell 调用 place_cell，已在未使用的新部署 `experiments/deployments/full-amf-incremental-site-import-06` 复用相同 site 分组导入；只包含活动 RM 的请求，静态位置审计和 route-only 精确导入门禁保留。该部署保持本轮原始 SRL 元数据约定以维持全量/增量一致。

072 已恢复有限依赖队列 `experiments/evidence/static-containment-01/incremental-after-recovery-072-03`。只有 V0 恢复后端完整成功、布线/DRC 验收通过及可用内存达到 50 GiB，才执行两路并行 RM 增量和最终拼接。新增量目录为 `experiments/runs/getrf-full-amf-contained-incremental-072-03`；队列失败不会自动重试或重跑全量。最新 PID 与状态以 launch.json 为准。

## 05:34 HKT：修复布局后审计对 Vivado 复制实例的误判

两端 backend-03 的 `place_design` 均成功，随后旧审计错误退出，尚未执行布线：

| 项目 | 072 V0 | 099 V1 |
|---|---:|---:|
| Vivado placement | 1314.115 s（21 min 54.115 s） | 1092.720 s（18 min 12.720 s） |
| placement 退出码 | 0 | 0 |
| 布局后审计 | 21.371 s，失败 | 18.830 s，失败 |
| 首个未识别实例后缀 | ap_CS_fsm_reg[8]_replica | ap_CS_fsm_reg[10]_replica |

Vivado 物理优化会新增高扇出复制寄存器；原审计在优化后仍要求所有 fabric 实例都来自 AMF 原始导出，因此把这些新增实例误报为导出遗漏。修复保留导入时严格的完整覆盖、唯一性和区域检查；优化后分别记录原有、新增、删除实例，所有实际 fabric 实例（包括复制实例）仍须通过同一硬区域合法性检查。新增实例不会计入 AMF 原始 LOC/BEL 保留率的分母，也不冒称 AMF 放置了新增实例。

9 个独立审计回归通过，包括新增、删除、等数量替换不能掩盖导入遗漏，以及新增静态/RM 实例越界均应失败。冻结部署为两端 `experiments/deployments/full-amf-postopt-audit-07`。当前还需真实运行验证布局后审计，不将模拟回归视为完整验收。

旧流程在审计之后才写 placed DCP，失败导致已完成 placement 无法恢复；两端只保存了 imported DCP。新流程先保存成功 placement/route 的 DCP，再执行审计，验收要求不变。此次只能从各自 imported DCP 重做后端 placement，AMF、输入导出和导入步骤均不重跑。失败尝试耗时保留为额外恢复开销，不混入成功运行的 placement 指标，也不能从实际项目总耗时中抹去。

- 072：`experiments/runs/getrf-full-amf-contained-v0-072-backend-04`，runner 1483649，05:33:15 启动。
- 099：`experiments/runs/getrf-full-amf-contained-v1-099-backend-04`，runner 1226391，05:29:23 启动。
- 恢复来源的 imported DCP SHA256：072 `250b9497dc10d1692e25d33bfd0004d8c5d02ea6e7aaaaa002714e36a0108632`；099 `dd105704ef74eb61dd2fb0c4e670f544a065bc1376498628cc524d1e0b360e64`。

截至 05:34，099 从 DCP 重开后的严格导入覆盖/区域审计通过（44.246 s），进入 placement；072 正在打开 imported DCP。两端 CPU 有活动、内存充足，尚无本轮 routed DCP。

旧增量队列 03 因父后端失败正常退出，没有启动任何增量任务。新有限队列 `incremental-after-recovery-072-04`（PID 1483820）仅修改依赖父运行和输出目录，仍使用冻结增量部署 06、当前配对的原始 SRL 元数据及其余相同配置。它等待 V0 backend-04 完整验收后再并行启动两个 RM；没有自动重试或重复全量 AMF。


## 05:57 HKT：V1 进入布线，实际验证布局后审计修复

099 backend-04 placement 成功（1068.257 s），写 placed DCP 73.488 s，布局后审计 45.629 s 通过。855,088 个原始实例全部存在，40 个 Vivado 新增复制实例全部通过硬区域检查，没有删除原始实例。原始实例 LOC/BEL 精确保留数为 855,086，仍为前述两个 LUTRAM BEL 差异；新增实例单独列出。

已启动 route_design，目前处于 Build RT Design。placed DCP 位于 099 experiments/runs/getrf-full-amf-contained-v1-099-backend-04/base_placed.dcp，只在服务器保存。布局后 WNS +0.697 ns、TNS 0；WHS -0.272 ns、THS -300.866 ns、9,745 个 hold 违例端点。这些是布线前估计，不能当作最终验收。

072 backend-04 placement 同样成功（1223.696 s），正在保存 placed DCP。增量队列仍等待 V0 完整布线、静态检查及输出成功；未重复运行 AMF。


## 06:07 HKT：两端进入布线，跟踪构建模型耗时

072 已保存 placed DCP（86.791 s）并通过布局后审计（51.491 s）：855,092 个原始实例均在，24 个新增复制实例区域正确，无删除；855,090 个原始 LOC/BEL 精确保留。布局后 WNS +0.450 ns、TNS 0；WHS -0.264 ns、THS -321.691 ns、10,383 个 hold 违例端点。此为布线前结果。

两端正在 Build RT Design。06:07 采样时099最新日志距采样约828秒，072约387秒；各自5秒采样均约100%单核CPU、VmSwap=0、RSS稳定约23GiB。历史 getrf-dfx-v0-shell-resume-03 / getrf-dfx-v1-full-resume-03 同名阶段为148/149秒，但旧实验没有本轮相同静态区域containment及AMF布局，不能作为单变量因果比较。记录为待跟踪的新增耗时；当前无错误、无证据支持停止或修改约束。


## 06:17 HKT：布线模型构建持续，暂未定位根因

06:17 两端仍在 Build RT Design，072 距最后日志987.7秒、0991430.9秒。5秒CPU采样分别100.07%/100.08%（单核口径），RSS分别24,000,588/24,273,868KiB、Swap均为0，读写计数相较上一采样未增加。没有新增错误；尚未出现实际布线拥挤与最终时序报告。不能仅凭该日志停顿认定卡死。两端系统采样权限限制为 perf_event_paranoid=4、ptrace_scope=1，未修改权限或附加调试器。官方文档检索未发现直接对应的性能修复；不据此更改静态containment或再次启动全量任务。增量队列04保持等待。本次证据见 monitor/20261013-221728.json。


## 06:27–06:32 HKT：保存慢阶段证据，启动只读成员诊断

两端布线模型日志分别约26.5/33.9分钟未更新，CPU仍为约100%单核，RSS和Swap均未恶化，没有新增错误。静态Pblock初始登记854233个primitive；099 placed DCP的late XDC为237134062字节，序列化静态成员798784项，其中存在层次成员，不能直接将此数等同叶实例数。大量显式成员是待验证假设，不是已确定的根因。

099独立只读诊断 experiments/preflight/static-pblock-membership-probe-01（runner1228398）读取已保存的placed DCP，计算实际层次下的成员闭包和潜在等价压缩。无约束修改、无placement/routing。当前两端正式路由以及增量队列04均保留。


## 06:53–06:57 HKT：等价压缩无收益，发现静态成员遗漏候选

原只读成员Tcl循环异常缓慢，核实进程身份后仅终止诊断Vivado PID1228451，现场及取消原因保存。改为一次性导出名称并在Python中计算闭包，真实DCP批量诊断总墙钟158.214秒、离线7.509秒。直接成员798784，实际primitive910111，静态闭包849226；候选成员仍798784，前后闭包SHA256同为fa060624223b9358b889529ac601a8b47bb4c439767082ea5f846994a8c7b3e7。严格等价条件下没有可用的压缩收益，未启动其后端比较。

与AMF原始静态fabric清单比对另发现4项未覆盖；072 placed DCP的序列化成员与祖先也未覆盖相同4项。当前prepare在建立静态成员后才执行opt_design，现有区域审计检查坐标但不检查完整Pblock成员归属。已启动099独立验证static-membership-repair-check-01：实际API核对4项归属，仅确认后补入独立设计的静态Pblock；全量primitive的LOC/BEL、固定标志和区域/时钟必须不变，单独保存DCP，不运行placement/route。该候选缺漏尚不能解释长时间Build RT Design。正式两端后端和增量等待队列均未停止、重启或放松约束。


## 2026-10-14 07:03–07:12 HKT：静态成员漏约束已实证

- 099 的独立 Vivado API 检查确认 4 个静态 primitive 没有任何 Pblock 归属（1 LUT4、2 FDRE、1 SRL16E）。加入 pblock_static 后，910111 个 primitive 对象的 NAME/LOC/BEL/固定标志、区域和时钟均保持不变；独立 DCP 已保存服务器。诊断耗时 274.501 秒，未执行 placement 或 routing。
- 新冻结部署 experiments/deployments/full-amf-static-membership-08 增加批量成员检查：opt 后及 route 前补入无归属静态实例；不允许将其他 Pblock 的成员转移进来，动态实例误入静态区则失败；route 后只检查不修补。12 个层次闭包回归通过。两端真实 placed DCP 的通用验证在 experiments/preflight/static-membership-generic-check-02 运行，072 PID 1500994、099 PID 1230537。此部署尚未替换正式后端。
- 072 与 099 旧部署存在两项哈希检查脚本差异，新部署保留各自原文件；成员修复相关四个脚本一致。未覆盖其他已有修正。
- 两端正式 backend-04 仍停留于 Build RT Design，07:03 采样距日志更新约 62.0/69.4 分钟，CPU 约 100%（单核）、RSS 约 22.9/23.1 GiB、swap=0。只能确认异常慢，尚不能判断死锁或确定原因。
- 为定位实际调用栈，在 099 新建独立限时诊断 route-build-stack-probe-02（PID 1230866），只调试新建子进程，不 attach 正式任务。首个诊断因 GDB 系统 Python 与 Vivado Python 环境冲突在启动时退出，未到 routing；第二个诊断分离两套环境。采样不作为性能对照，也不修改正式结果。
- 为避免自动使用漏约束的 shell，已停止尚未启动 RM 的等待队列 1483820，保存 held-for-membership.json。正式 V0/V1 布线继续；增量任务待完整静态成员和 shell 隔离验证通过后重新挂接。未重跑 AMF。

4 个成员漏项是正确性问题，但与 Build RT Design 过慢的因果关系尚未证明。完整 routed WNS/DRC/pr_verify/bitstream 和 70% 比较仍未完成。


07:13 HKT diagnostic update: route-build-stack-probe-02 exited before route_design after a startup SIGSEGV under GDB in libxv_isl_iostreams. No useful routing stack was captured. This does not establish a fault in the two formal Vivado processes. Both generic membership validation processes remain active; no additional router diagnostic launched.

## 2026-10-14 07:34 HKT：静态成员修复验收与 backend-05 恢复

本轮定位并修复了一个实际约束遗漏：两个已完成 placement 的设计中，均有 4 个真实静态 primitive 未属于 pblock_static。独立验收仅增加这 4 个成员，不修改时钟、Pblock 范围、CONTAIN_ROUTING、placement 或固定标志。

| 项目 | eda072 / V0 | eda099 / V1 |
| --- | ---: | ---: |
| 全 primitive 属性核对数 | 910,099 | 910,111 |
| 补回静态成员数 | 4 | 4 |
| 实际静态 primitive 数（排除常量） | 849,214 | 849,230 |
| 第一次完整成员检查＋修复 | 11.525 s | 10.748 s |
| 第二次幂等检查 | 11.480 s | 9.991 s |
| 独立验收总墙钟（包含读写 DCP 和全量属性核对） | 342.955 s | 299.697 s |
| LOC/BEL、固定标志、全部 Pblock 约束、时钟 | 均不变 | 均不变 |

旧通用 Tcl 校验在两端触发了独立诊断的 900 s 超时。命令级 trace 显示所有批量查询已经结束，慢点在 Vivado 内嵌 Tcl 的纯名称分类循环；同一分类算法在独立 Tcl 中耗时约 1.44 s。新部署 full-amf-static-membership-09 将纯名称分类交给隔离的系统 Python，Vivado 仍负责真实对象查询、修改及校验。12 个 Tcl 用例和 6 个 Python 用例通过；上表是真实完整 DCP 验收，不是模拟测试。没有证据证明名称分类慢点与 route_design 内部慢点相同。

验收输出（均只保留在所在服务器）：
- 072：experiments/preflight/static-membership-external-check-04/membership_repaired_placed.dcp，SHA256 31372b5ca1501885e3f0630fbdaaa25071a5aaf9ce3edcaec22c94f4f51bf59f。
- 099：同相对路径，SHA256 0bb976277daf9a7f807c6f018c7683df017685470874f1f15b40e4d96d8fc391。

旧 backend-04 在停止前均仍处于 Build RT Design；072/099 日志分别约 90/98 分钟未更新，但采样约 100% 单核 CPU、RSS 稳定、无 swap。不能据此认定死锁。因为已经证实静态约束成员遗漏，已保存现场并仅对各自实验 Vivado PID 发出 SIGTERM，让原 runner 正常记录失败退出。正式 GUI 和其他实验未改动。取消证据为各 backend-04/cancellation-static-membership.json。

两端已分别从原 base_placed.dcp 启动 backend-05：
- 072：getrf-full-amf-contained-v0-072-backend-05，runner PID 1505363。
- 099：getrf-full-amf-contained-v1-099-backend-05，runner PID 1232068。

恢复继续使用相同 AMF 输出和已完成 Vivado placement，预布线阶段补齐成员后写 base_membership_placed.dcp，再开始 route_design。**不重跑 AMF 或 place_design**。这是约束正确性修复，尚无证据说明它能改善 Build RT Design 耗时。

增量队列 04 保留为已停止历史；新队列 05（PID 1505612）绑定修复后的 V0。必须等父任务完成布线和 DRC、布线后静态成员检查零遗漏、RP routing footprint 中没有未解释的 static_only 候选，才启动两个 RM。遇到该候选时先分析真实节点/导线占用，不能直接认定违规，也不能忽略。

最终 routed WNS、完整布线、静态保持、pr_verify、完整/局部 bitstream 以及 70% AMF/routing 比较仍待完成。Vivado placement 继续单列；既有 AMF 测量为 V0 1969.035 s、V1 1498.189 s，本次恢复耗时不计入 AMF placement。

07:37 HKT 实际恢复核对：两端均已补回 4 个成员并成功保存 base_membership_placed.dcp；预布线成员检查分别 11.407 s / 9.904 s。099 已通过 placed 区域审计；072 正在该审计。没有重新执行 place_design，日志无 Error/Critical Warning。两端 Vivado PID 为 1505420 / 1232121，增量队列仍等待。

07:52 HKT：修复后的 backend-05 两端仍在 Build RT Design，约 10 分钟；采样约 100% CPU（一个核），RSS 约 17.8 GiB、无 swap、无新错误。因此四个成员修复尚未消除准备慢点。已有静态 Pblock 最终 GRID_RANGES 仅 25 段，不能把耗时直接归因于几百个删除范围。独立诊断 route-build-containment-isolation-01（072 runner PID 1508874）已启动：同一个已修复 place 后 DCP，仅在独立内存设计关闭静态 CONTAIN_ROUTING，核对其余布局/固定/几何属性不变，在进入 Phase 2 或限时后停止，不输出 DCP/bitstream，不属于任何验收或性能对照结果。正式实验约束保持不变。


2026-10-14 08:06 HKT：独立关闭静态 CONTAIN_ROUTING 的诊断已结束。相同的 base_membership_placed.dcp，910099 个 primitive 对象的 LOC/BEL、固定标志及所有 Pblock 几何不变，仅静态 CONTAIN_ROUTING true→false，两个动态 RP 的限制保持开启。route_design 调用到进入 Phase 2 的观测上界 166.036 s；Vivado 单列 Phase 1 的 elapsed 为 136 s。诊断在 Phase 2 主动停止（退出 -15 为预定停止），没有写 DCP 或 bitstream，也不是正式验收结果。

正式 backend05 两端仍在 Phase 1，采样约 100% CPU，RSS 17.8 GiB、swap 0，无 Error/Critical Warning；其余流程尚未完成。该结果指向静态 containment 的处理开销，但恢复检查点及先前 report_timing 的影响尚需排除。已启动相同诊断流程、CONTAIN_ROUTING 仍为 true 的对照 route-build-containment-control-02，072 runner PID 1510635；输入 DCP、脚本流程、4 线程和 10 ns 均相同，路由 600 s / 总计 900 s 限时。正式任务与约束保持不变，增量队列05仍在等待。


2026-10-14 08:18 HKT：同流程 containment 开启对照达到 600 s 路由限时，仍在 Phase 1，按计划结束（-15）；总诊断墙钟 817.183 s。关闭对照从 route_design 调用至 Phase 2 的上界 166.036 s。两者使用同一修复后 DCP、同一 runner，Tcl 仅 containment true/false 及说明标签不同，910099 个对象位置/固定标志和 Pblock 几何不变。该对照支持静态 containment 在本设计上触发模型构建额外开销；不能据此确定是成员数量、几何形状还是 Vivado 内部算法，也不代表关闭限制后的完整布线成功。正式 backend05 两端仍在 Phase 1（约40分钟），未放松约束。完整 AMF 每端只成功执行一次，后续一直复用其输出；审计失败曾迫使 Vivado placement 各额外执行一次，已保留恢复开销。新增量尚未启动。


### 2026-10-14 09:37 HKT：Build RT Design 异常定位到静态 containment 的成员/区域处理

结论：本次长时间停留不应解释为正常的静态布线复用成本。当前两端是从已完成 placement 的全量 DCP 执行 route_design，shell 锁定在全量布线后才发生；AMF 已完成。直接慢点是 Vivado 为 net 构建 containment 布线范围/掩码约束时的 Pblock 类型及成员层次检查。

受控证据：

| 诊断/运行 | 输入 | 变更 | Phase 1 结果 |
| --- | --- | --- | --- |
| isolation-01 / 072 | 修复成员后的 V0 placed DCP | 仅关闭静态 CONTAIN_ROUTING | Vivado Phase 1 136 s；route 调用至 Phase 2 观测上界 166.036 s |
| control-02 / 072 | 同一个 V0 DCP、同一诊断流程 | 静态 containment 保持 true | route 600 s 限时后仍处于 Phase 1 |
| geometry-isolation-06 / 099 | 修复成员后的 V1 placed DCP | 静态 containment 和全部 798788 直接成员保留，仅改为覆盖全器件的矩形 | Phase 1 249 s；route 至 Phase 2 观测上界 276.037 s |
| 正式 backend-05 / 两端 | 各自 V0/V1 DCP | 原 25 段范围及隔离要求 | 09:36:41 HKT 核对仍无 Phase 2；日志至少 117.4 / 118.2 分钟未更新 |

几何对照验证了 NAME/LOC/BEL/IS_LOC_FIXED/IS_BEL_FIXED 以及成员列表不变。扩大区域使其与 RP 重叠，因此仅为诊断，不是合法的隔离方案，不参与 WNS/加速比验收。它说明大成员数本身不足以解释持续超长时间，几何相关 containment 处理会触发或放大该成本；不能把两个不同 V0/V1 的短时数字直接比较为同输入性能收益。

native-stack-05 在 Phase 1 后 60 s 采样，捕获到并行实例/BEL 延迟初始化，尚不足以定位持续慢点。GDB 正常退出并 detach；旧 runner 只识别另一种 detach 提示而错误记录 stack_captured=false，原 manifest 保留，capture-review.json 记录了纠正。没有因为这个状态误判再跑同一个早期采样。

late-stack-07 对同一个 V0 DCP 保持全部约束，在 Phase 1 后约 300 s 及 345 s 两次采样。两次均落在：
```text
HARTDesign::buildRtDesign
  -> HARTNetConstraintMgr::addHDBBoxConstraints
  -> HARTNetConstraintMgr::addHDBBoxConstraint
  -> getContainedRouteAreaRouterWrapper / getMaskRegions
  -> HDHDPartPinMgr::getContainedRouteArea
  -> HDHDManager::isReconfigPblock
  -> isHDReconfigModule / getParentInstanceLH
```

对服务器原 Vivado 2026.1 动态库只读检查表明 isReconfigPblock 包含 createNonRepeatableInstanceIterLH 成员遍历、isHDReconfigModule 判定和 getParentInstanceLH 父层次循环。因此“反复处理 Pblock 成员与祖先层次”有函数实现及两次运行栈支撑。当前静态 Pblock 直接成员约 79.88 万，非恒量 static primitive 闭包约 84.92 万。大量 net 的区域约束查询与大成员扫描结合，是高度可信的性能放大机制。尚未记录总调用次数、每次扫描数或证明严格的复杂度；也未取得 AMD 对具体工具缺陷的确认。

正式两端的 20 s 线程采样各只有一个工作线程约 100% CPU，其余线程基本等待；进程 RSS 约 17.8 GiB，VmSwap=0。这不支持“内存换页或仅增加线程能解决”的解释。只表示采样期间该步骤为单工作线程瓶颈，不意味着整个 router 都串行。

范围生成来源为 scripts/dfx_static_containment.tcl：先加入全器件 clock regions，再减去两个 RP 的 routing footprint 对应 site 范围，最终得到 25 段混合 clock-region/site 范围。段数本身不算多，不能笼统归咎于“几百段碎片”。几何对照与函数采样共同把问题缩小到这种静态 containment 的实际区域约束构建路径。先前补齐四个静态成员修复了正确性遗漏，但没有消除该慢点。

后续修复方向：先验证保持原隔离语义的等价区域/成员表达能否避开重复扫描；任何改法均需核对允许区域、static/RP 归属、placement 以及最终静态布线隔离。不能以关闭静态 containment、覆盖整片矩形、随意拆分静态 Pblock、增加线程或放松 DRC 当作已验证解决方案。若等价表达仍触发该路径，则应保留最小复现提交工具侧定位。

本次三个诊断均已终止各自进程，未改动正式运行、Vivado GUI 或正式约束；未重跑 AMF，未生成诊断 DCP/bitstream，未恢复定期检查。两个正式 backend-05 及依赖队列保持原状态。证据目录：
- experiments/preflight/route-build-native-stack-05
- experiments/preflight/route-build-geometry-isolation-06
- experiments/preflight/route-build-late-stack-07

