# 静态引脚访问缓存与 AMF FF Q 打包过滤：59

## 状态和范围

本次独立副本实现、180 秒限额缓存认证和 fresh A/B 完整增量验收均已完成。原始证据复核时间：2026-10-18T02:52:52.748523+08:00；服务器：eda072。没有重跑全量。

复用已验收 V0 的空 A/B 静态壳、静态连接与最终位置，以及已有完整 SLICE/DSP 第一跳访问分类。不重新提取、不重跑全量、不执行增量 `place_design`。自动监控保持暂停，本轮为用户授权任务中的人工核验。

## 实现

1. `scripts/certify_native_pin_access.py` 对父缓存、空静态壳、提取来源与完整归档做 SHA 和覆盖绑定。逐行读取全部 A/B 分类，只从“所有映射出口都被静态占用”的真实 FF Q 输出推导硬限制；没有设计 cell 名或坐标白名单。总流程由外层 180 秒截止时间保护，失败产物不得用于布局。
2. `StaticDesignCache` 新增 AMFSTATIC4。保留旧 v2/v3 读取能力；完整分类压缩归档、覆盖、来源和稀疏 FF Q 掩码写入不可变子缓存。旧静态图和原有元数据逐字节保持，归档嵌入后逐 SHA 核验。父缓存保留。
3. 增量模块通过 `fixed-static-ff-q-v1` 策略仅激活对应模块的掩码。每个 SLICE 使用 16 位 FF BEL 掩码，普通模式缺省不激活，无新增 Vivado 查询。
4. AMF 打包候选按每个控制组 bank 的可用 FF 数量检查完整分配，保留时钟、复位、FF 类型、主槽与 CARRY/MUX 限制。必要时在兼容 bank 间寻找分配；最终槽位和输出再校验，禁止选中被屏蔽 BEL。
5. A/B 从 OOC 综合、优化、导出到 AMF 和模块坐标导入并行，之后导入同一静态壳并联合布线。没有调用旧的事后 FF 坐标修补器。

完整分类有 7,684,560 条 BEL-pin 记录（A/B 各 3,842,280）。本次数据推导出 A 的 SLICE_X80Y178/AFF2、B 的 SLICE_X96Y358/CFF2 两个硬限制。它们是输入数据结果，不是代码内坐标规则。

分类仍是第一跳访问证据，不能证明任意后续网络必定可路由。LUT/DSP 的部分阻塞、接口同网、常量及专用连接保留分类，本轮不整体禁止相应 BEL。模块更换且静态壳/器件/区域保持一致时可复用；静态布线、壳或区域来源变化须重新认证，不能沿用失效掩码。

## 已实测新增成本

| 项目 | 秒 |
| --- | ---: |
| 已有数据与来源核验 | 2.603 |
| 完整分类读取和限制推导 | 4.286 |
| 原生缓存加载、扩展和写出 | 37.857 |
| 完整归档与原静态图读回核验 | 4.639 |
| 输出哈希与 contract | 2.385 |
| 缓存生成完整墙钟 | **51.877** |

缓存增加 102,571,478 字节。上述 51.877 秒复用已有完整提取结果，不代表新静态壳从零提取也能在 180 秒内完成。本轮没有重新测量冷提取。

实际 A/B AMF 中分类载入/解码及新增过滤计时 scope 合计分别为 0.031027/0.028424 秒；其中容量和槽位过滤累计均低于 0.001 秒。这些是线程 scope 计时，不是整个增量关键路径新增时间，未包含原有大缓存的完整 I/O 和哈希。

同一 build3 loader 对原缓存和扩展缓存做三组成对加载：均值 22.503188/22.501098 秒；配对差值 +0.210560、−0.805288、+0.588457 秒，未分辨出稳定加载回退。单次 SHA 均值增量约 0.066592 秒。样本少、操作系统缓存较热，并与 joint Vivado 共享主机；不能把 −0.002090 秒均值差宣称为加速或严格上界。这项校准与静态壳打开阶段重叠，保留其对实际墙钟的可能影响。

一次性跨服务器 staging 为 784.029679 秒（10 个文件、4,883,581,505 字节）；构建、历史提取和故障诊断均另计，不属于每轮增量固定开销。数据通过服务器间流式传输，DCP、bitstream、完整缓存不落本地。

## 实验来源与验证边界

- 源基准：dd6a643d582275a357b2c1c4e90de4335f2ecd3c + 59 冻结 overlay。
- 本轮 AMF build3 SHA：806a164ba06630417b9a4dfe8b2ca0d8a4c0af2aa792c9577fdf69f248754100。
- 缓存 SHA：3e8fa9871889517e818b9a236197f0e1d5cdb4c6047c7b13b4394bc54fd1ef9e。
- 增量部署 SHA：29a7f7e0367f774640f9d3bf6a014a53fa38619dbeb88a2cbb5ca15d87ca2cac。
- V0 缓存的原始 producer 仍为已验收 48 binary；缓存认证程序和本轮打包 binary 分别追溯，不声称重新执行了同 binary 全量。
- 12 ns、右三个 clockregion、AMF 12 线程、Vivado 2026.1 四线程、普通 Pro/ProPrimitiveTiming=false 保持原配置。
- NativeStaticCache、BoundarySitePolicy、CLBSiteLegality 回归通过。没有重跑普通模式全量；普通模式“不受影响”限于关闭路径、旧缓存兼容和相关回归证据。
- 两个 AMF 分支均完成，所有 5,912 个位置避开硬掩码。相对旧失败 49 的输出，A 完全不变，B 有 4 个 BEL 变化、site 变化为 0。新 AMF 的坐标在模块导入和共同静态壳导入后均保持。
- 保留已有 Route 35-4475 incomplete placer guidance tree 日志告警；实际时钟来源 jtag/TCK 与 12 ns 单独核验，不能把 DRC 报告 0/0 等同于日志没有 Critical Warning。
- 静态逐 cell membership 扫描按原 skip 配置，独立 pr_verify-full_check 必须实际通过。


## 最终验收与实际主流程时间

- 全布通：922760/922760；路由错误 0。
- DRC Error/Critical Warning：0/0。
- WNS +0.033 ns、WHS +0.001 ns；setup/hold/pulse-width 均通过。
- 导入及最终路由后 5,912 个动态 LOC/BEL 精确保持，硬掩码使用数 0。
- 实际 12 ns、jtag/TCK 来源及时钟覆盖通过；no_clock 和 unconstrained_internal_endpoints 各 1，与基准一致，不称为零未约束。
- pr_verify -full_check 通过；full 与 A/B partial bitstream 均生成并重新核验 SHA。
- 日志仍有 Route 35-4475 告警；独立 DRC 报告的 0/0 不代表日志没有告警。

| 主流程项目 | 秒 |
| --- | ---: |
| A 完整分支（fresh 综合开始至 module DCP 完成） | 331.373718 |
| B 完整分支 | 277.005423 |
| A/B 实测并行跨度 | 331.373768 |
| A/B 实测重叠 | 277.005372 |
| 共同静态壳打开及身份检查 | 154.793 |
| 共同壳导入 A/B | 512.917 |
| 导入后检查及 imported DCP | 267.192 |
| 一次联合 route_design（Tcl 外层实测） | 883.720 |
| routed DCP 写出 | 53.819 |
| joint 后端进程（包含上述阶段，不能重复相加） | 1993.898620 |
| 必要最终验收及 bitstreams 墙钟 | 971.556440 |
| **从 fresh 综合起的实际主流程总墙钟** | **3302.076631（55.03 分钟）** |

AMF 进程：A 190.191441 秒，B 137.307714 秒。没有增量 Vivado placement，故本轮实际主流程与“扣除 placement”口径相同。不得把 A/B 各阶段最大值相加冒充真实并行跨度。

最大完整分支+joint 进程+最终验收模型为 3296.828779 秒；与实际主流程的差额 5.247852 秒是启动、校验和管理等间隙。详细利用率与 100 条路径后台报告不计主流程。一次缓存认证、跨机 staging、构建和历史诊断另计；本轮 cache-load 校准与静态壳打开共享资源，不能把本轮总耗时差异全部归因于打包补丁。

### 产物（仅在 eda072）

- `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-12ns-right3-072-v1-incremental-pincache-59/joint/joint_routed.dcp`
  - SHA256 `e1af4fcc1c4750acaaa9d35aa55c46a01c771b065f3e4bc3268b87d614e49e6c`，415581103 字节。
- `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-12ns-right3-072-v1-incremental-pincache-59/acceptance/bitstreams/full.bit`
  - SHA256 `d8b56c38c98126e65b3f33935c1b14ee1ed2eebf600efedf0f21cd03f283d722`，113318589 字节。
- `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-12ns-right3-072-v1-incremental-pincache-59/acceptance/bitstreams/full_pblock_a_partial.bit`
  - SHA256 `a2efb827306249d2de62070d1f49ddfb3e1be8aa4c3afb077d9e82f26028989c`，2970006 字节。
- `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-12ns-right3-072-v1-incremental-pincache-59/acceptance/bitstreams/full_pblock_b_partial.bit`
  - SHA256 `855eb0186aa9d8ff7aec8c129c783e427bd3dfc6c9b097fb16f4b08c81f77e86`，2966562 字节。

### 最终验收原始计时

- qor：open_routed 137.958s；audit_joint 239.932s。
- static：pr_verify 548.540s。
- bitstreams：open_verified_routed 140.870s；full_and_partial_bitstreams 265.250s。

QoR 与 static 分支并行；以上阶段不能直接全部相加当作墙钟。所有 source、build、cache、deployment、输入及产物哈希均在 59 独立目录保留。最终证据为 final-acceptance-review.json，完整 DRC 报告继续留服务器；轻量镜像记录未下载的大文本列表。
