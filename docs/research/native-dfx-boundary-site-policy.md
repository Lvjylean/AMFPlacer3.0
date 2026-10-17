# DFX 增量布局边缘站点规则

2026-10-16，在独立副本 `edge-policy-15` 实现用户确认的规则：对以完整 clock region 构成的矩形 Pblock，动态 SLICE 和 DSP 留空最上、最下各一行站点。左右边缘只有中间位置实际存在 `PROHIBIT` 时才扩展整列；角点不触发整列限制。BRAM 不增加几何留空，但仍遵守真实 `PROHIBIT`。

实际 A/B 布局验证已通过，各 2956 个动态 cell 均未落入禁止站点。`flow34` 已把两份新布局通过 `place_cell` 导入共同静态壳并成功联合布线，实际 `route_design` 为 696.369 秒。正式路由后 DRC、时序、`pr_verify -full_check` 均已通过，完整和两份局部 bitstream 已生成并核对 SHA。旧 `flow33` 的 56 条 PLDL-1 错误记录保留。

## 实际边界与资源代价

同一已验收静态壳的只读查询确认以下站点范围。这里的“一行”使用各资源自身的 site Y 坐标，不把一个 SLICE 行误当成一个 DSP 行。

| 模块 | 资源 | 原站点范围 | 新布局排除行 | 排除站点数 |
| --- | --- | --- | --- | ---: |
| A | SLICE | X31–116 Y60–119 | Y60、Y119 | 172 / 5160，3.33% |
| A | DSP48E2 | X4–15 Y24–47 | Y24、Y47 | 24 / 288，8.33% |
| B | SLICE | X31–116 Y300–359 | Y300、Y359 | 172 / 5160，3.33% |
| B | DSP48E2 | X4–15 Y120–143 | Y120、Y143 | 24 / 288，8.33% |

SLICE 左右列的 `PROHIBIT` 全部位于与上下边缘重合的角点；侧边中间为 0。DSP 左右列全部为 0。因此本 A/B 不额外排除左右整列。静态壳中的 301 个真实禁用站点（A 148、B 153）均已被上述上下留空覆盖，不能再把这 301 个重复计为额外资源损失。

规则作用于整个 Pblock 的外边界。多 clock region Pblock 内部的普通分界不会产生额外留空带。当前支持完整 clock region 组成的连续矩形；部分站点范围、非矩形、缺失资源清单或留空后资源耗尽均明确报错，避免静默采用错误几何。

## 在 AMF 内部落实

首次全量验收时，Vivado 一次性导出各 Pblock 的 `GRID_RANGES`，以及 site 名称、类型、clock region、`PROHIBIT`。这一步只提供原始属性；`BoundarySitePolicy.h` 在 C++ 内推导留空集合，`StaticDesignCache` 将结果与静态身份、器件、周期和静态壳 SHA 绑定后冻结。

后续增量加载缓存时，AMF 将该模块的留空集合合并到已有 `blockedSites`。它复用现有增量候选站点、容量、合法化、打包和最终导出检查入口；专用 DSP 级联仍由原来的逐站点合法性检查覆盖。没有修改普通 placer、时序模型或专用级联定义。

静态 cell 的 LOC/BEL、固定 RP 接口、Pblock 定义和路由 footprint 均保持原语义。留空约束只作用于新动态 cell 的可放位置，不收缩路由器可以使用的区域，也不移动已有静态 cell。

旧缓存可通过 `scripts/certify_native_resources.py` 生成独立子缓存，原缓存不覆盖。当前这次只读资源查询耗时 133.679 秒，包含约 127 秒打开 DCP，属于一次性缓存准备成本；后续增量无需再次打开静态 DCP 查询边缘。

## 默认行为与接口

普通全量和旧增量配置的默认策略为 `off`。启用此策略需要在实验 job 设置：

```json
{
  "native_dfx_edge_policy": "clock-region-slice-dsp-v1",
  "native_dfx_edge_side_policy": "interior-prohibit"
}
```

AMF 内部配置使用 `"Native DFX edge policy": "clock-region-slice-dsp-v1"`。新全量缓存捕获仅在显式启用时增加资源属性导出。普通全量的布局算法和既有输入路径不改变。

带资源认证的缓存使用 `AMFSTATIC3` 文件标识；旧格式保持 `AMFSTATIC2`。新二进制可读取旧缓存，旧二进制明确拒绝新缓存，避免忽略新限制。启用边界策略却使用未认证旧缓存、错误器件/周期/静态壳或不支持的二进制会拒绝运行。新缓存中的真实 `PROHIBIT` 总是作为物理限制生效；额外边缘留空由显式策略开关控制。

## 验证与实测

最终构建通过原缓存 45 项、位置修复 14 项、边界策略 25 项 C++ 检查，以及 8 个 Python 测试方法。边界测试覆盖角点不扩列、侧边中间禁用触发该侧、DSP 与 BRAM 区别、多 clock region 内部边界、固定静态不移动和非法输入拒绝。

实际 A/B 在策略默认关闭并读取原缓存时，10 份原有输入/上下文输出逐 SHA 与历史结果完全相同。开启策略后，固定静态位置、RP 端点、接口缓冲、cell 清单和原 Pblock 允许站点全集仍保持字节一致；只有禁止候选站点集合及其审计元数据按规则改变。

| 实测项 | A | B |
| --- | ---: | ---: |
| 动态 cell 完整覆盖 | 2956 | 2956 |
| 边界及禁止站点违规 | 0 | 0 |
| AMF 进程墙钟 | 268.976 秒 | 151.395 秒 |
| AMF 初始化 profile | 39.546 秒 | 39.564 秒 |
| AMF run inclusive profile | 227.605 秒 | 109.954 秒 |
| 相对原 flow29 坐标发生变化的 cell | 2944 | 2924 |

A/B 同时启动，实际 AMF 并行跨度 268.976 秒。验证程序总墙钟 352.413 秒，还包括一次性子缓存生成、默认关闭兼容对照和结果校验；不能将其称为完整增量编译时间。本轮复用既有 OOC 综合、优化和模块网表，未重新计时这些阶段。AMF profile 包含既有诊断成本，不称为纯算法时间。

这次在新资源约束下重新求解了 A/B，属于边界策略验证，不是只移动原来的 56 个违规 cell。原问题 DSP 从历史 X15Y47 改为当前 X15Y46；原相邻 DSP 的位置也随新布局调整。本轮新的联合布线已通过，边缘留空本身仍不是一般路由可行性的数学保证。

## 构建与证据来源

服务器独立目录：

```text
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/preflight/20261015-native-static-cache-27/edge-policy-15
```

- `repo-final/`、`build-02/`：最终源码和构建（C++ 文件逐 SHA 与 build-02 冻结源相同）；二进制 SHA `0d0c0ab8e16940b21f74b1f1fe6456fcc138542fefb34d6db9e2dd6cebd13036`。
- `validation-01/`：实际 A/B 并行 AMF，生产二进制 build-01 SHA `005177cf817a2c8497ba71255c8275ffd7d090951245d00e1ef64c307d822f53`。
- `validation-02/cache/`：最终格式子缓存，SHA `e1fe349bc60b6d5d5f40a366fb96cfcf0db801d4fc7da71f431dbb7d76fe43c4`。
- `validation-03/`：最终缓存与实际 AMF 输入的完整对照、旧二进制拒绝和增强 Python 测试。
- `query/`：只读原始资源查询，源 DCP 前后 SHA 不变。
- `evidence/implementation-results.json`：资源规则、完整坐标变化及构建差异。

build-01 到 build-02 的 C++ 差异只有缓存格式识别保护及相应测试，布局引擎和边界集合推导未变。两版本所有共享的 11 份模块输出及诊断网表已逐 SHA 相同；因此复用已完成布局，不冒称实际布局由 build-02 重新产生。首次兼容验证脚本误从普通 AMF 工作区寻找仅 `compose-inspect` 才生成的 `netlist.txt`，保留了失败记录；后续验证复用已有结果，对照相应诊断文件完成，没有重跑 AMF。

完整上游源为已有 dirty 冻结副本，不把基准 commit 单独当作源代码身份。完整文件哈希、父子关系和构建来源保存在 manifest。代码修改仅在独立副本，监控自动化继续暂停。

## 联合布线验收

服务器新运行：

```text
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-native-static-cache-099-18-flow34
```

两个模块并行读取已完成 OOC optimized DCP，创建 AMF 内部规划的接口缓冲，并通过 `place_cell` 导入新坐标。随后共同静态壳一次性导入两份模块，执行一次 `route_design`。全程不执行 `place_design`。

最终检查包含所有动态坐标完整保留、真实 `PROHIBIT`、RP 与接口/footprint、全路由、DRC Error/CriticalWarning、setup/hold、专用级联、`pr_verify -full_check` 和完整及两份局部 bitstream。静态逐 cell 扫描按既有配置跳过；实际 full pr_verify 单独报告。利用率和详细 100 条时序路径在验收完成后后台生成，不计入主流程。

A/B 导入阶段均通过，实际 `place_cell` 为 0.759/0.747 秒。接入共同静态壳分别为 232.545/212.311 秒，导入审计 226.013 秒，写导入 DCP 53.794 秒。实际完整 `route_design` 调用为 696.369 秒；路由器内部的 590.66 秒未覆盖后续 DFX 检查等步骤，不能替代完整调用计时。

正式 QoR 报告确认 922887 条 routable nets 全部布通，routing errors 0，DRC Error/CriticalWarning 均为 0，WNS +1.494 ns、WHS +0.009 ns，setup/hold/pulse 均无违例。全 5912 个动态 cell 在布线后仍精确保留 AMF LOC/BEL。`pr_verify -full_check` 521.768 秒通过。

路由日志保留既有的 1 条 Route 35-4475 时钟引导树警告，不能把“最终 DRC CriticalWarning 为 0”扩大成“所有工具日志无 Critical Warning”。完整和两份局部 bitstream 已生成，bitgen 报告 0 Critical Warning、0 Error。产物只保存在服务器。

## 本轮阶段计时与口径

| 阶段 | 实际时间（秒） | 口径 |
| --- | ---: | --- |
| A/B AMF 并行布局 | 268.976 | 两进程最早开始至最后完成；单进程 A 268.976、B 151.395 |
| A/B 并行坐标导入 | 31.503 | 含进程启动、读取 OOC DCP、接口缓冲和写模块 DCP |
| 打开共同静态壳及核验 | 134.735 | 132.541 + 2.194 |
| 向共同静态壳导入 A/B | 444.856 | 同一设计内依次导入，232.545 + 212.311 |
| 导入后审计 | 226.013 | 保持必要检查 |
| 写共同导入 DCP | 53.794 | 不含任何 Vivado placement |
| 联合 route_design | 696.369 | 完整调用时间 |
| 写最终 routed DCP | 52.132 | 服务器落盘 |
| QoR 与 full pr_verify 并行验收 | 525.711 | 实际进程跨度；QoR open 144.040 + audit 207.084，pr_verify 521.768 |
| bitstream 阶段打开已验收 DCP | 133.713 | 独立阶段 |
| 完整及两份局部 bitstream | 271.677 | 一次生成，未重复生成局部文件 |
| **后端实际总墙钟** | **2617.783** | 含后端准备、导入、布线、验收、bitstream 与调度开销；不含之前的 AMF |

后端总墙钟为 43.630 分钟。上表细分计时与进程跨度具有不同边界，不能简单相加替代实际总墙钟。本轮复用已完成 OOC 综合、优化和导出网表，因此 **2617.783 秒不是从 RTL 重新综合开始的完整增量编译时间**；A/B AMF 268.976 秒另外列出。利用率和 100 条详细关键路径按用户要求交给后台生成，排除于主流程时间。

一次性边界资源查询 133.679 秒、新格式构建配置/编译 5.686/72.103 秒、兼容验证中的失败与修复分别保留原记录。从本轮边界查询开始到后端主流程完成的实际修复验证窗口为 3952.664 秒（65.878 分钟），其中包含构建、默认关闭对照、版本兼容验证和开发间隔，不能称为正常增量编译基准。此前失败、旧后端和诊断开销也没有从历史记录中删除。

本次实际执行 `place_design` 次数为 0，“扣除 Vivado placement”的后端调整值与原始后端墙钟相同。不能据此改写以前实际执行过 placement 的历史统计。

## 最终服务器产物

服务器：`coldsun`（099）。下列文件均未下载到本地。

```text
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-native-static-cache-099-18-flow34/joint/joint_routed.dcp
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-native-static-cache-099-18-flow34/acceptance/bitstreams/full.bit
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-native-static-cache-099-18-flow34/acceptance/bitstreams/full_pblock_a_partial.bit
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo/experiments/runs/getrf-native-static-cache-099-18-flow34/acceptance/bitstreams/full_pblock_b_partial.bit
```

| 产物 | SHA256 |
| --- | --- |
| joint_routed.dcp | `e728b8684647ab2bb9461959a08598aa2e51b514d0604ba9d9ef5e8bf3268436` |
| full.bit | `15210e456efdebc6599d9c0c380390843b30acacec2f1a0e94ed34d42a63ba35` |
| full_pblock_a_partial.bit | `5c407555acdf3a3c0fbd8b06e5e7d8d31222b6a4c2d7a5a6243113a947e46a79` |
| full_pblock_b_partial.bit | `1f832e3b0d9e4682a6246cc3883fb2716877c45b7a10a74cd7b7c76962bd5ed4` |

最终汇总在 `edge-policy-15/evidence/final-validation.json`，含文件尺寸、哈希、原始阶段计时和验收结果；`parent-inputs-final-check.json` 确认原 A/B 各 18 个来源文件哈希不变。恢复索引指向 `repo-final`、`build-02` 和 `validation-02/cache`，不使用原型缓存 `validation-01/cache`。监控自动化保持暂停。
