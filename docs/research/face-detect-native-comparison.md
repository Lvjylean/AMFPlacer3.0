# faceDetect：Vivado 原生与 AMF 混合流程对照

实验日期：2026-09-25。服务器：eda072（ee4e072），账户 jinyang。

**本轮原生 Vivado 流程完成，耗时 280.910 秒；现有 AMF 混合流程耗时 1,452.523 秒，是原生的 5.1708 倍。两者均通过最终时序检查，原生的建立时间余量多 0.095 ns。** 这是每条流程各一次运行的结果，不代表所有设计或多次运行的统计结论。

## 对照条件

| 项目 | 条件 |
| --- | --- |
| AMF 实验 | `faceDetect-preplacement-20260925-204725-194072` |
| 原生实验 | `faceDetect-vivado-native-20260925-214940-832956` |
| 输入 | AMF 实验保存的同一份布局前 opt DCP；原生实验独立复制快照 |
| 输入 SHA-256 | `9f5d9f2b560a781785acd2bdfce0ff0f777b8aa12d0b954c01a4148da656789a` |
| 器件 | `xcvu095-ffva2104-2-e` |
| 输入网表检查 | 两轮均在 Vivado 实现前核对 134,449 个单元名称及类型 |
| Vivado | 2024.2，SW Build 5239630，两轮版本字符串一致 |
| Vivado 线程 | `set_param general.maxThreads 4` |
| 实现指令 | `place_design -unplace`、`place_design`、`route_design`；默认选项，无额外 `phys_opt_design` 命令 |
| 后端约束 | 两轮均保留同一输入 DCP 内的时序和物理约束 |
| 报告 | 相同的实现前 timing/DRC，以及实现后 route/DRC/timing/utilization/bus-skew 报告 |
| 输出 | 两轮均写出最终 routed DCP，均不生成 bitstream |

原生 Tcl 从上一轮实际保存的后端脚本生成，替换 AMF 位置导入为上述 Vivado 指令，增加计时包装并更改输出文件名；完整差异保存在原生实验 `provenance/native_vs_amf_route.diff`。运行入口提交：`8169588f`。

AMF 自身仍采用已保存的用户目标 `ClockPeriod=15 ns`，并使用 8 个 jobs。Vivado 自身的优化和最终报告使用 DCP 内的多个时钟及完整约束。这是当前配置下的工作流对照，不能解释为两个布局算法使用了完全相同的内部约束模型。

## 编译时间

均采用实际墙钟时间（elapsed），不采用多线程 CPU 时间相加。总时间统计脚本的编译子进程阶段，包含启动、输入检查、报告和最终 DCP 写出；不包含实验准备时的文件快照复制，以及编译后的独立只读审计。

| 环节 | AMF + Vivado | Vivado 原生 |
| --- | ---: | ---: |
| 新网表导出阶段 | 869.738 秒 | 不需要 |
| AMF 进程 | 81.942 秒 | 不运行 |
| Vivado 后端整体 | 500.843 秒 | 280.910 秒 |
| **三阶段总计 / 原生总计** | **1,452.523 秒（24 分 12.523 秒）** | **280.910 秒（4 分 40.910 秒）** |

下面是后端整体时间中的部分子项，不能再叠加到上表总计。AMF 后端使用日志按秒取整的耗时；本轮原生使用新增 Tcl 毫秒计时。

| 后端子项 | AMF + Vivado | Vivado 原生 |
| --- | ---: | ---: |
| 打开 DCP | 约 19 秒 | 18.985 秒 |
| 导入 AMF 位置 | 上轮没有独立计时，已包含在后端整体中 | 不需要 |
| `place_design` | 约 86 秒（基于 AMF 位置补全/优化） | 97.560 秒（原生布局） |
| `route_design` | 约 102 秒 | 113.933 秒 |
| 写最终 DCP | 约 9 秒 | 4.959 秒 |

AMF 进程的 81.942 秒比原生布局命令的 97.560 秒少约 15.6 秒，但实际混合流程还需要位置导入及约 86 秒的 Vivado 布局补全。只报告 82 秒会遗漏这些成本。

即使仅做算术扣除、假设输入导出包已经可用，AMF + 后端仍需 `81.942 + 500.843 = 582.785 秒`（9 分 42.785 秒），约为原生总时间的 2.075 倍。该数字不是另一次缓存实验，也不是增量编译性能；网表或约束变化后是否可复用导出包，仍需正确的缓存失效判断。

## 最终时序和质量

| 指标 | AMF + Vivado | Vivado 原生 |
| --- | ---: | ---: |
| WNS（建立时间最差余量） | 0.056 ns | **0.151 ns** |
| TNS | 0 | 0 |
| 建立时间失败端点 | 0 | 0 |
| WHS（保持时间最差余量） | 0.030 ns | 0.029 ns |
| THS | 0 | 0 |
| 保持时间失败端点 | 0 | 0 |
| 脉宽失败端点 | 0 | 0 |
| Bus skew 违例 | 0 / 8 行检查 | 0 / 8 行检查 |
| Bus skew 最差余量 | 7.236 ns | 6.943 ns |
| 布线错误 | 0 | 0 |
| 可布线 / 完成布线网络 | 113,122 / 113,122 | 110,993 / 110,993 |
| DRC Error / Critical Warning | 0 / 0 | 0 / 0 |
| DRC Warning / Advisory | 288 / 40 | 288 / 40 |
| 固定 LOC/BEL 保留 | 25 / 25 | 25 / 25 |

两轮的逻辑网络总数均为 255,069；内部已路由网络分别为 127,928 和 130,057，因此外部可布线网络数不同，不应将该差值解释为输入网表不一致。建立、保持和脉宽的报告端点总数也分别一致。

本轮原生建立时间余量更大；AMF 的保持时间与 bus skew 余量略大，且两者这些项目都满足约束。DRC 规则及计数完全一致，包括涉及 6 条 PCIe 状态信号无可布线负载的 `RTSTAT-10` Warning。

## 结果和复现

在服务器项目根目录执行：

```sh
python3 scripts/amf3.py native-run --reference-run faceDetect-preplacement-20260925-204725-194072
python3 scripts/amf3.py status --run faceDetect-vivado-native-20260925-214940-832956
```

最终原生 DCP 仅保存在服务器：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/faceDetect-vivado-native-20260925-214940-832956/reports/faceDetect_vivado_routed.dcp
```

大小：47,477,914 字节。SHA-256：`46d85da94c8be5329766c949e257cc7d28c7da6fb30bf2f99bac3571a52ca689`。

同轮 `reports/amf_comparison.json` 保存两轮比较；`verification_summary.json`、`stage_times.tsv`、各项 `.rpt` 和 `logs/01_vivado_native.log` 保存原始指标及命令耗时。固定位置审计单独保存在 `reports/fixed-audit/`，其耗时不计入编译。

本地下载报告使用：

```sh
python3 scripts/sync_reports.py faceDetect-vivado-native-20260925-214940-832956 --ssh-hostname 143.89.78.72
```

11 项回归测试已通过，包括原生指令与参考一致性、Tcl 分段计时、elapsed/CPU 时间区分，以及既有输入和审计测试。后续速度研究应同时优化网表导出、位置回传和布局补全，并分别报告核心阶段与端到端耗时。
