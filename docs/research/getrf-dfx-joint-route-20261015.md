# GETRF/U250 20ns：公共静态 shell + AMF 布局直接联合布线实际验收

本次新流程的原生功能验收通过。完成时间：2026-10-15T13:08:02.246551 HKT；报告整理时间：2026-10-15T13:14:17.872203+08:00。全程未执行 Vivado `place_design`，没有 placer 兜底，没有重跑 AMF 或全量参考。

完整联合 V1：922875 个可布线 net 全部布通、路由错误0，DRC Error/Critical Warning=0/0。WNS **+1.494 ns**、WHS **+0.009 ns**，TNS/THS=0、setup/hold失败端点均0，WPWS=9.300 ns、pulse失败端点0。A/B各2956动态cell的AMF导出位置要求（site及导出时指定的BEL）完整覆盖，导入后和路由后均0变化。`pr_verify -full_check` 实际通过，完整及两份显式局部bitstream均成功写出并复核SHA256。

单次联合 `route_design` 实测 **1075.485s**，为匹配全量V1的 **32.0362%**，低于70%限额2349.9616s。该结论属于有明确来源限制的兼容性分析：A使用固定邻居修复二进制，严格同二进制条件不满足；本轮复用已有AMF结果，没有重新测量两AMF并行的墙钟。因此不宣称“严格同二进制且两AMF新并行运行”的综合70%证明已经成立。历史独立RM流程的70%失败保留。

## 实际执行流程与验证范围

当前独立run：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23`，冻结部署 `full-amf-static-shell-route-only-23`。原生Vivado2026.1、4线程、20ns；AMF来源保持普通Pro、ProPrimitiveTiming=false、12线程、原RP/SLR/距离系数与SRL语义。static CONTAIN_ROUTING关闭、没有强制static Pblock；动态DFX约束保持。没有调整周期、线程、牵引、预算或DRC等级。

1. 本次迁移时从已完成的本轮V0基准一次性生成公共静态shell，同时清空旧A/B，`lock_design -level routing`，写出并重开验证。两RP都IS_BLACKBOX=1、内部cell=0，各316接口pin完整保持。后续消费者直接使用该空RP输入，不重复移除旧A/B。
2. 消费者读取已验收的公共静态DCP，再次拒绝任何仍保留实现RM的输入。导入两份在任何历史Vivado placer/router之前提取的AMF placed RM：包含优化网表及全部坐标；不消费独立路由的RM DCP。
3. 在同一完整静态设计内复核全部5912cell、硬RP/SLR、allowed/blocked masks、20ns、接口及RP footprint；写出 `joint_amf_placed.dcp`，直接一次联合 `route_design`。
4. 路由后再次完整核对AMF位置、区域、接口、footprint，完成全布通、DRC和min/max时序审计、`pr_verify -full_check`、完整与A/B局部bitstream。

原生阶段表没有 `vivado_place`；完整日志没有placer命令或启动banner，只有一次实际 `Command: route_design`。消费者没有旧RM移除阶段。worker的未来契约是完成AMF坐标导入与placed模块导出后结束，等待联合后端验收；本次实际验证复用已有placed模块，不冒称新的worker冷启动端到端运行也已重新实测。

## 静态、接口与专用资源检查

公共静态DCP：

```text
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/common_static/common_static_locked.dcp
```

SHA256：`b3bb1f048da6b330687d3ebfb24fa4eaae5066481f71845bec959aa7d80448a2`。实际空RP写出/重开以及消费者加载检查均成功。接口快照各316pin与工具partition-pin统计口径不同，所有已分配PPLOC及合法空PPLOC均保留，未仅选择部分引脚。

`pr_verify -full_check` 对照V0完整基准，实际输出compatible，比较计数如下：

| 原生统计对象 | 参考DCP | 新联合DCP |
|---|---:|---:|
| reconfigurable modules | 2 | 2 |
| partition pins | 630 | 630 |
| static tiles | 57462 | 57462 |
| static sites | 100775 | 100775 |
| static cells | 868756 | 868756 |
| static routed nodes | 16650509 | 16650509 |
| static routed pips | 15622726 | 15622726 |

上述是Vivado比较口径，不是AMF动态fabric数量。参考全V0 DCP没有static锁定标志，工具HDPRVerify-38/43的Info原样保留；公共静态生产阶段确实执行了routing lock，新联合设计通过完整比较。额外逐cell静态快照扫描按既有配置明确skip，不能把它写成已执行通过。

专用资源/级联合法性依据真实router DRC、整设计default DRC和全部bitstream写出前置DRC；未额外声称执行了独立逐级联拓扑扫描。普通Warning仍按原等级保留，包含DSP流水建议和路由中的局部SLL需求提示；最终Error/Critical Warning为0/0。导入后曾有RTSTAT-1/2/9三项未布线状态Critical Warning，路由后全部清零，没有降级。资源表static_cells=0不代表RP内没有静态wire；本轮在保留静态布线的完整上下文中联合求解。

## 原始耗时与口径

| 当前联合消费者原生阶段 | 秒 | exit |
|---|---:|---:|
| open_common_static_shell | 204.935 | 0 |
| verify_common_static_input | 3.002 | 0 |
| import_amf_placed_a | 330.215 | 0 |
| import_amf_placed_b | 328.473 | 0 |
| audit_imported | 345.164 | 0 |
| write_joint_imported | 81.939 | 0 |
| vivado_route | 1075.485 | 0 |
| write_joint_routed | 80.808 | 0 |
| audit_joint | 230.430 | 0 |
| pr_verify | 695.209 | 0 |
| full_bitstream | 322.603 | 0 |
| partial_a | 123.139 | 0 |
| partial_b | 125.260 | 0 |

`vivado_route` 是完整一次命令，包含原生初始化、时序、寻路/优化和DFX收尾，不称为纯寻路循环时间。审计包含全设计route/timing/DRC报告，不称纯格式转换。full_bitstream命令还自动产生DFX分区输出，随后显式写出a_partial/b_partial；这些成本全部计入，未扣除。

本轮直接消费已经导出坐标的module DCP，AMF Tcl/placement.tsv格式核对属于预检与Python管理部分，没有新的独立“适配wall”测量；不把未单独测量写成0。AMF原始导出self、历史模块提取以及本次native导入各按实际证据分别列出。

| 实际窗口/统计 | 秒 | 说明 |
|---|---:|---|
| 公共静态shell生产进程wall | 673.738957 | 一次性，包含读参考、移除旧RM184.672s、lock53.405s、写出/重开/验证 |
| 公共shell生产控制器子命令wall | 674.304066 | 含Python启停；与上一行嵌套 |
| 联合消费者原始wall | 4060.623855 | 包括预检、读入/导入、报告、route、DCP、full_check、bits、hash和启停 |
| 消费者timed原生阶段之和 | 3946.662000 | 是上一行的组成部分 |
| 消费者未包timed的残差 | 113.961855 | 包含独立route-status/footprint/utilization/critical-path、预检/启停等，不从wall隐藏 |
| 本次控制器完整实际wall | 4743.461246 | 2026-10-15T11:48:58.785319→2026-10-15T13:08:02.246551 HKT；包含公共shell、两次外部预检、消费者全部步骤 |
| 本次启动至报告生成的实际历时 | 5119.086872 | 另包含完成后的SHA复核、结果读取、工具等待和报告整理；最终同步回执见report-delivery.json，不称算法时间 |
| 扣除Vivado placement的消费者调整值 | 4060.623855 | 本次place未执行，故与原始wall相同 |
| 扣除Vivado placement的整次调整值 | 4743.461246 | 同上；没有从历史wall中悄悄删除placement |
| 从原16轮worker prepare起至本次完成的实际跨度 | 30918.513017 | 2026-10-15T04:32:43.733534+08:00→2026-10-15T13:08:02.246551+08:00；含失败/修复、旧后端/拼接/诊断、joint21、flow23及排队/人工间隔，不能称纯算法时间 |

本次没有新的AMF/input prepare成本，但继承成本不是0：原A/B prepare2968.359583/2969.460967s，恢复复制0.423577s，成功AMF进程279.426507/127.573543s。该执行窗口使用缓存，不能把4743.461s当成重新冷启动并行AMF到bitstream的实测端到端时间，不能直接据此宣称端到端加速比。原16轮起跑前的shell传输172.814s另列，不包含在其后起算的campaign跨度内。

## 匹配参考与AMF profile

全量V1：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-v1-099-03`；route3357.088s，AMF进程1418.428454s，profile placement1338.764103603s，实际Vivado placement1375.279s，原始wall8082.351737s，扣Vivado placement调整6707.072737s。全量参考WNS+0.614/WHS+0.009ns、923447个routable net全布通、DRC0/0；SLL21085是该全量实测值，本次未重做同一SLL统计，不冒充本轮值。

| 同口径profile证据 | A继承结果/s | B继承结果/s | 全量V1/s |
|---|---:|---:|---:|
| initialization inclusive | 9.366741671 | 9.226029615 | 67.107598040 |
| AMF run inclusive | 267.572898678 | 116.049392992 | 1339.863417351 |
| export_vivado各项self之和 | 0.008034330 | 0.007194275 | 1.099313748 |
| run inclusive减上述export self | 267.564864348 | 116.042198717 | 1338.764103603 |
| dumpFinalPacking inclusive（嵌套，不相加） | 254.640005531 | 92.252518118 | 12.539904280 |

AMF主指标保留诊断成本，A/B为全量19.99%/8.67%，不能称纯算法。进程wall减profile差值不等于export；inclusive子阶段也不能重复相加。两成功AMF历史实际没有重叠，process span2158.564s、gap1751.564s，原并行placement70%证明未成立；本次没有用理论max或工作量之和替换该事实。旧独立routes跨度3267.066s=97.32%仍为原70%性能失败；新流程只用实际一次联合route1075.485s比较，不拿旧routes之和或跨度冒充。

A二进制 `609ec41f8da157103efa845c81510ae14c00b95979b02ffb82e335570381ef54`，B/全量 `e097c317b7fff66e3ddd1a5291c86d1a49f7b1bfa589f96d181c9be73217df90`。A仅固定静态邻居packing-site缺失的坐标读取函数修复；B新二进制同输入158.227355s复算证明2956位置一致、lookup0、HPWL68194.400812一致，只限本B输入。基准commit b5322e1fb5ff3b862c00a8b97bc963f84494ff70为dirty冻结源码基准，source.patch及overlay另追溯，不伪造同一binary。

## 继承准备、修复、旧流程与输入优化

旧source模块读取/认证/提取发生在joint21任何placer/router之前：A174.721/8.303/90.228s，B162.194/8.049/87.014s。本轮复用完成的module DCP，没有重复这些步骤。历史joint21已于2026-10-15T12:11:15.791157完成，其place1755.037s、route1052.235s、raw wall6650.792820s均保留；不是本次route-only结果。新旧输入路径和缓存复用不同，不把总wall差值全部归因于移除placer。旧拼接已按用户指示停止，未恢复。

本轮继承/诊断证据记录如下。各项有重叠，不能相加当总墙钟；完整起跑/排队/恢复间隔已包含在上述真实campaign跨度。A失败AMF27.824796s、恢复复制0.423577s和传输172.814s另保留。仅metadata预检失败的15/20未启动native后端，不重跑；没有未记录的本轮flow23 native重试。

| 历史证据目录 | 状态 | 来源计时/秒 |
|---|---|---:|
| build-20261015-dfx-static-neighbor-fix-01 | failed | 2.532 |
| build-20261015-dfx-static-neighbor-fix-02 | completed | 14.417 |
| 20261015-dfx-static-neighbor-b-replay-01 | completed | 158.227 |
| 20261015-scoped-input-query-profile-02 | completed | 180.299 |
| 20261015-scoped-input-net-cache-03 | failed | 900.002 |
| 20261015-scoped-input-array-cache-05 | completed | 499.137 |
| 20261015-scoped-input-array-cache-b-06 | completed | 468.275 |
| 20261015-route-audit-query-profile-07 | completed | 194.809 |
| 20261015-route-audit-array-candidate-08 | failed | 1800.002 |
| 20261015-route-audit-phase-profile-09 | completed_readonly_phase_profile | 255.639 |
| 20261015-route-audit-exact-vector-cache-10 | failed | 181.873 |
| 20261015-route-audit-name-vector-cache-11 | failed | 3600.002 |
| 20261015-route-audit-name-only-sample-12 | completed_sample_equivalence_only_not_adopted | 244.091 |
| 20261015-route-audit-native-filter-sample-13 | completed_sample_equal_not_adopted | 455.508 |

输入优化A05/B06各14文件（9生成+5继承）逐SHA exact，原linked DCP/golden未变；连接、顺序、常量/clock、DSP端点、SRL、固定上下文及mask保留。scoped输入1944.589→337.050s、1958.244→314.300s，分别5.77/6.23倍；含DCP打开的进程499.137/468.275s。没有重做全部link/opt/resource表，不能声称原49.5分钟完整cold prepare已变为8分钟。修复在未来部署19冻结（scope SHA b0cc1e5ada363990042a22a7ea644f6ec9154f792efa18dc38e64f2198b040f5）；本次23复用旧输入，未采用其冷prepare路径，也未为输入优化重跑已成功AMF。样本query02没有提速，未采用；03/08/11按预算结束，并非完整等价通过，10截断错误未接受结果，12/13仅样本且不快，均未用于本次新路由流程。更早metadata15的管理失败未单独取得可证wall，不补造为0，原失败记录保留。

## 服务器产物与追溯

以下真实文件均仅保存在coldsun，不下载DCP或bitstream：

```text
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/joint/joint_amf_placed.dcp
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/joint/joint_routed.dcp
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/joint/full.bit
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/joint/a_partial.bit
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-full-amf-uncontained-srlfix-20ns-route-only-099-08-flow23/joint/b_partial.bit
```

| 文件 | 字节 | SHA256 |
|---|---:|---|
| joint_amf_placed.dcp | 436762080 | `9bfdd908bb2a0b94c3d6859eac3bc566d3461e36da4da26fe9e67d6b521a1630` |
| joint_routed.dcp | 415601605 | `68794d4405d2065768fd9b5c31ba0483ab5c381b7fa9c909990cda7871a7fb7a` |
| full.bit | 113318589 | `9c87318d1db02ea9f0c444b0065b6d1ceafeddec689a53cf5f5f7482c132c4db` |
| a_partial.bit | 2970006 | `dbe32df166b8750313d8bb9da6c3132678b356a99190dc4b41339efe1282204e` |
| b_partial.bit | 2995274 | `cafdf74d1cf121dcf212b90b11ee2ec702aca0d38ef3981c07e4cb8590fde7de` |
| vivado_placement_not_executed.txt | 88 | `59bc0e15dd0f35f98dab8bb69945dd08c1ce005208a1b43057d33f174c174cff` |

Vivado完整位流命令还保留自动的 `full_pblock_a_partial.bit`、`full_pblock_b_partial.bit`；正式交付的局部文件是上表显式命名的a_partial/b_partial。公共空RP静态DCP供后续复用，完整V0参考仍原样保留用于追溯和full_check。

部署manifest SHA256：`1a2ad954b5851a66659b71befdf332b567d03ac0116874e00f40d6ad0dac6d49`，92项冻结源码哈希终检通过；输入、公共shell contract、两份module和所有正式输出均终检SHA通过。所有源码/脚本/配置与父子关系、native阶段区间、退出状态、profile及输出哈希保存在：

```text
experiments/evidence/static-containment-01/srl-corrected-14/experiment-launch-03/joint-route-transition-20/route-only-23/
  completion-evidence.json
  native-pr-verify-full-check.txt
  amf-profile-source-summary.json
  inherited-cost-provenance.json
  related-joint21-final-provenance.json
```

此前joint21逐次进度文档保存在同目录 `report-before-native-completion.md`，没有改写历史run manifest。自动监控继续按用户要求PAUSED，本次一次性计算已完成；不恢复旧assembly、诊断、已取消实验或重复AMF/全量。
