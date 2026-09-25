# faceDetect 布局前 DCP 全流程验证

日期：2026-09-25。服务器项目根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

**本次完整验证成功。** 从 `opt_design` 后、`place_design` 前的完整设计出发，新网表导出、AMF 布局、Vivado 布局补全和布线均已完成，最终时序与 bus skew 检查通过。

| 检查 | 最终结果 |
| --- | --- |
| 输入网表 | 134,449 个叶子单元，0 个黑盒，25 个固定位置 |
| 导出 / AMF / Vivado 后端耗时 | 869.738 / 81.942 / 500.843 秒，三个阶段退出码均为 0 |
| 三阶段耗时合计 | 1,452.523 秒，约 24 分 13 秒；另有输入校验和最终只读审计 |
| RAM 宏 | 255 个 LUTRAM 由 AMF 原生路径处理，无外部物理宏输入 |
| 布线 | 113,122 / 113,122 条可布线网络全部完成，0 个布线错误 |
| 建立 / 保持时序 | WNS=0.056 ns，TNS=0；WHS=0.030 ns，THS=0，无失败端点 |
| Bus skew | 8 行检查，0 违例，最差余量 7.236 ns |
| DRC | 0 Error、0 Critical Warning；288 Warning、40 Advisory |
| AMF 位置请求 | 133,562 个全部找到，132,515 个位置一致（99.2161%），1,047 个被调整，0 个未放置 |
| 固定约束 | 25 / 25 个 LOC/BEL 保持一致 |
| 最终网表 | 134,449 个输入单元全部保留、类型一致；Vivado 另外插入 1 个 BUFGCE |

DRC 相对布局前输入多 1 条 `RTSTAT-10` Warning，涉及 PCIe 的 `cfg_ltssm_state_reg0[5:0]` 共 6 条没有可布线负载的网络。其余 287 条 Warning 和 40 条 Advisory 与输入一致。最终严重性统计与此前历史 benchmark 实验一致，但本轮并非零警告设计。

最终 DCP 仅保存在服务器：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/faceDetect-preplacement-20260925-204725-194072/reports/faceDetect_amf_routed.dcp
```

文件大小 53,290,766 字节；SHA-256：`c96ebb7527b566cfab69e486d47ea0275a0b1ad4e8c5d02d011d80b5360e1580`。

机器可读证据位于同轮 `reports/verification_summary.json`、`placement_audit.json`、`final_netlist_comparison.json` 和各项 `.rpt`。本地仅同步报告、日志和配置。

## 输入与实验边界

原始输入（只读）：

```text
/Projects/haoning/Vivado_Results/faceDetect_project/VCU108_PCIE_faceDetection/VCU108_PCIE_faceDetection.runs/impl_1/design_1_wrapper_opt.dcp
```

Vivado 2024.2 打开该 Vivado 2023.2 checkpoint 后，得到 134,449 个叶子单元、0 个黑盒。只有 25 个单元具有 LOC，且均为固定单元。顶层 synth DCP 仅有 11 个黑盒，不能单独用于此次完整验证，因此选择已经链接完整的 opt DCP。

输入 DCP 的 SHA-256：`9f5d9f2b560a781785acd2bdfce0ff0f777b8aa12d0b954c01a4148da656789a`。

本轮采用：

- 从该 opt DCP 重新导出的单元、引脚、网络、时钟驱动名单及固定单元。
- AMF 原生 CARRY/MUX/BRAM/DSP 宏和单个 LUTRAM 保守占用一个 SLICEM 的已有路径。
- 关闭 `unpredictable macro file` 和 `designCluster`，由 AMF 计算聚类；不使用历史布局的宏坐标或历史聚类。
- 复用相同器件的资源表、兼容表和已经清洁构建验证的 AMF 二进制。
- AMF 时序目标保持用户配置 `ClockPeriod=15 ns`；Vivado 后端保留输入 DCP 内的约束，两者分别记录。

导出的原始 unpredictableMacros 文件仅用于追溯，不被 AMF 读取。没有位置的 RAM 并不因此被删除。固定单元文件补上明确表头，适配旧读取器无条件跳过首行的行为；校验记录数与 DCP 一致，防止首个固定单元丢失。AMF 核心算法源码本次没有更改。

运行目录会保留历史 benchmark 文件的快照用于对比；实际输入以 `config.json` 为准，不能因文件存在就认为本轮使用了旧网表或旧宏布局。

## 可复现入口

在服务器项目根目录执行：

```sh
python3 scripts/amf3.py run --config configs/experiments/face-detect-opt.json
python3 scripts/amf3.py status --run <run-id>
python3 scripts/verify_face_detect_flow.py experiments/runs/<run-id>
```

最后一条只在流程完成后执行，打开最终 DCP 核对 AMF 请求位置与 25 个固定单元约束，并生成 `reports/verification_summary.json`。审计保留 Tcl 列表语义，不执行输入的单元名称。

当前实验：`faceDetect-preplacement-20260925-204725-194072`。运行入口提交 `2ed7d71d`；审计与回归测试提交 `91324bbc`。实际布局二进制来自 `builds/build-20260925-152056-398298-7ed7e39f/build`，二进制哈希在每轮 manifest 中核对。

本地同步报告：

```sh
python3 scripts/sync_reports.py faceDetect-preplacement-20260925-204725-194072 --ssh-hostname 143.89.78.72
```

启动阶段还保留了一轮 `faceDetect-preplacement-20260925-204605-664053`：新增状态检查的 Tcl LOC 过滤式引号转义错误，在导出网表前退出。修复后创建新运行目录，没有覆盖失败证据。

首次最终位置审计将 27 个含反斜杠的 Tcl 请求名称误报为缺失；Vivado `get_cells` 解析后的 `NAME` 保留了不同的转义层级。只读查询确认其均存在，修复审计工具后重新检查通过，并记录 27 个名称别名。首次审计及脚本已存档到本轮 `work/audit-history/`。该修复没有重跑布局布线，也没有修改最终 DCP。8 项回归测试覆盖输入完整性、固定单元表头、Tcl 名称与转义审计、报告传输边界。

Vivado 导入日志记录 181 个初始失败批次，随后完成布局调整及布线。应以最终逐单元审计衡量位置保留情况，不能把初始失败批次数当作最终丢失单元数。

## 验证口径

分别记录导出、AMF、Vivado 后端耗时及退出码；核对全部导出单元的名称与类型；核对完整布线、布线错误、时序、DRC、bus skew，以及最终 AMF 请求位置保留率和固定位置一致性。

这轮不包含 RTL 综合、bitstream 生成、上板验证、功能等价证明或增量编译算法。Vivado 后端会执行 `place_design` 完成/调整布局，再执行 `route_design`；最终布局可能与 AMF 输出有差异。单个 case 通过也不代表任意综合网表、器件或外部宏格式都已支持。
