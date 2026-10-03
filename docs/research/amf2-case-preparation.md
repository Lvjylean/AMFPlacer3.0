# AMFPlacer 2.0 算例准备

2026-09-30，服务器 eda072，正式项目 `/Projects/jinyang/workspace/AMFplacer3.0`。

本轮整理上游文档列出的 8 个主要 benchmark，保留原始 VCU108 / `xcvu095-ffva2104-2-e` 设计、AMF 输入及约束；U250 迁移和正式布局布线未执行。额外保存仓库中的 Gemmini 输入及不完整的 BLSTM_DSPDomain 变体。

## 数据与来源

服务器数据根目录：

```text
/Projects/jinyang/workspace/AMFplacer3.0/data/reference/amf2-cases-20260930
```

主数据来自 [AMF-Placer 上游项目](https://github.com/zslwyuan/AMF-Placer) 的 `doc/pages/_3_1_BenchmarksDetails.md`、`_3_3_PostImplementationProjects.md` 及后者链接的 8 个公开 Google Drive 工程包。AMF 输入快照来自正式项目内 `benchmarks/VCU108` 和 `benchmarks/testConfig`；输入逐文件记录 SHA-256，不覆盖旧实验或上游文件。本轮没有使用其他账户的私有 DCP。

- `archives/`：8 个原始公开工程压缩包，共 4,616,148,836 字节。
- `projects/`：完整解压工程，含源文件、IP、XDC 和各阶段 DCP。所有成员 CRC 校验通过；原始工程中的绝对路径/IP 版本依赖仍需在重新综合前检查，不承诺在新版 Vivado 下可直接重建。
- `amf-inputs/`：原始 AMF 设计网表、时钟驱动列表、固定单元、宏、聚类、器件信息和兼容表快照。
- `cases/<case>/`：原始 JSONC、使用快照绝对路径的严格 JSON、单元清单、准备状态、DCP 配对与原始约束。
- `kernel_candidates.json`：5 个 HLS 应用计算核心的独立 IP DCP 候选位置。包含历史 cache 候选，尚未验证与所选顶层 DCP 的连接等价性；后续应优先核对 `.srcs` 中的当前 IP。
- `manifest.json`、`catalog.json`、`prepared_catalog.json`、`SHA256SUMS`、`expansion.json`：来源、阶段状态和完整性证据。

DCP 全部保存在服务器。本地只保存此说明、脚本、清单和轻量预检报告。

## 主要算例与规模

| 算例 | 应用 | 上游 AMF 单元记录数 | AMF 配置 ClockPeriod | opt DCP 的时钟个数 |
|---|---|---:|---:|---:|
| faceDetect | 人脸检测 | 134,450 | 15 ns | 9 |
| SpooNN / halfsqueezenet | 神经网络推理 | 137,937 | 8 ns | 7 |
| BLSTM_midDensity | 双向 LSTM | 215,101 | 8 ns | 7 |
| digitRecognition | 数字识别 | 265,775 | 8 ns | 7 |
| MemN2N | 记忆网络 | 289,721 | 10 ns | 7 |
| OpenPiton | 多核处理器 | 309,145 | 10 ns | 19 |
| OptimSoC | 多核 SoC | 468,150 | 20 ns | 12 |
| MiniMap2 / minimap_GENE | 基因序列比对加速 | 681,889 | 8 ns | 7 |

Gemmini 为补充输入：494,135 个单元记录，AMF 配置周期 23.8 ns；上游这组 8 工程的发布清单未提供其项目链接，因此仅标记为 AMF 输入已准备，不标记后端就绪。BLSTM_DSPDomain 只有 3 个数据文件，缺少独立 testConfig 和 clock 文件，不作为完整算例。

表中的 AMF ClockPeriod 是 placer 配置目标，不能当作全部 Vivado 时钟的周期。例如 faceDetect 的 AMF 配置为 15 ns，而原 DCP 的计算时钟 `clk_out1_design_1_clk_wiz_0_0` 为 20 ns，另有 PCIe/派生时钟。所有实际时钟、波形和生成关系保存在对应 `clocks.tsv`、`clocks.rpt`、`original_timing.xdc`；本轮没有用统一 10 ns 覆盖它们。

## 预检和限制

使用 Vivado 2024.2、每进程最多 4 线程，只执行 DCP 加载、身份导出和报告。8 个 opt DCP 全部成功加载，黑盒数均为 0。保存了 `check_timing`、时钟交互、时序例外及利用率报告。

所有 opt DCP 都与仓库中的 AMF 网表有不同程度的阶段差异，不能直接拼接。程序继续检查同包的 placed/routed/physopt 检查点，按单元名称和类型寻找匹配版本；每次尝试都保存输入哈希、工具版本、脚本哈希、耗时、退出状态和差异清单。配对成功仅表示单元名称/类型集合一致，尚未证明逐引脚连接等价，也未验证重新布局布线。

原始 opt 阶段的约束覆盖：

| 范围 | no_clock | 未约束内部端点 | 缺少 input delay | 缺少 output delay |
|---|---:|---:|---:|---:|
| faceDetect / SpooNN / BLSTM / digitRecognition / MiniMap2 | 0 | 0 | 1 | 3 |
| MemN2N | 0 | 0 | 3 | 4 |
| OptimSoC | 0 | 0 | 3 | 3 |
| OpenPiton | 3,828 | 260 | 4 | 7 |

OpenPiton 的 AMF 文本网表还有 12 个重复名称（每个出现两次），因此 309,145 条记录对应 309,133 个唯一名称。已核对每组完整 cell/pin/net 记录逐字节相同，并保存 `duplicate_cell_names.json`、`duplicate_block_audit.json`。原文件保留，另生成 `derived-inputs/netlist-unique-cells.zip` 和 `inspect_unique_config.json`，仅删除 12 条完全重复记录，其余 cell 数据不变；派生 ZIP 的 SHA-256 为 `6812b84c146420f60f3258aa37edac83b18b9408411f78aa690f673efd0c69f2`。未修改名称、连接、时钟或时序例外。其 routed DCP 与 309,133 个唯一单元的名称/类型匹配，但时钟覆盖仍需审查。其他 8 份完整配置（含 Gemmini）的 AMF 单元名称未发现重复。

现有案例比 ISPD2017 算例提供了更丰富的原始时序信息，但都不能仅凭本次准备宣称完成板级时序签核。原目标器件、工具版本、时钟关系和时序例外应纳入后续对照协议。


## 最终配对与加载结果

8 个主要算例均找到与上游 AMF 唯一单元名称/类型集合匹配的 DCP：

| 算例 | 选用阶段 | 唯一单元数 |
|---|---|---:|
| faceDetect | placed | 134,450 |
| SpooNN | routed（impl_2） | 137,937 |
| BLSTM | routed | 215,101 |
| digitRecognition | routed | 265,775 |
| MemN2N | placed | 289,721 |
| OpenPiton | routed | 309,133 |
| OptimSoC | routed | 468,150 |
| MiniMap2 | placed | 681,889 |

每个主要 case 的 `cases/<case>/reference.dcp` 指向该检查点，`backend_pair.json` 保存精确路径、SHA-256、成功/失败候选和报告位置；原始 opt DCP 仍保留。将这些检查点用于新的 placement 时，需要在独立实验中清除既有位置/布线并按协议保留约束，不能直接把已有 placed/routed 状态当成新结果。

最终选中的 OpenPiton routed DCP 的 `no_clock=3840`、`unconstrained_internal_endpoints=260`，与上表 opt 阶段的 3828 个无时钟端点区别记录；其余 7 个主要算例的选定 DCP 两项均为 0。缺失 I/O 延迟计数与上表一致。

使用标准 `scripts/amf3.py inspect` 和已记录构建 `builds/build-20260929-185044-227956-c70df682/build/AMFPlacer`，9 份完整配置（8 个主要 case 加 Gemmini）全部以退出码 0 完成输入加载，`placement_executed=false`。OpenPiton 使用 `inspect_unique_config.json`，实际加载 309,133 个单元；其他使用 `inspect_config.json`。运行索引为 `experiments/preflight/20260930-amf2-input-loading/summary.json`，每个输入检查都有独立 manifest、status、日志与输入统计。输入检查读到的是原始 VCU108 单 SLR 设备，不代表 U250 移植或完整后端已通过。

仅重新进行输入检查的例子（在服务器项目根目录执行）：

```sh
python3 scripts/amf3.py inspect \
  --config data/reference/amf2-cases-20260930/cases/face-detect/inspect_config.json \
  --binary builds/build-20260929-185044-227956-c70df682/build/AMFPlacer
```

## 用于 U250 的边界

原完整系统包含 VCU108 对应的 `PCIE_3_1`、`GTHE3`、MMCM/PLL 或 DDR BITSLICE 等资源。完整系统移植需要重建/适配平台接口；计算核心实验则需要明确剥离边界、重新导出输入和建立 OOC 约束。不能只替换 device 字符串、删掉时钟或忽略专用资源来宣称 U250 已就绪。

建议从 faceDetect、SpooNN 开始做计算核心迁移预检，再加入 digitRecognition / BLSTM。完成 U250 转换后，应使用同一个转换后 DCP 分别导出 AMF 输入和建立 Vivado 对照；不得把原始 VCU108 网表与新器件 DCP 混用。是否实际跨多个 SLR 必须由最终布局验证，不能仅凭器件是 U250 推断。

本轮未执行新的 AMF placement、Vivado place/route、物理优化、IP 升级或重综合。

## 脚本与证据

- `scripts/diagnostics/prepare_amf2_cases.py`：快照输入、下载、选取候选 DCP。
- `scripts/diagnostics/expand_amf2_projects.py`：完整解压与所有成员 CRC 核对。
- `scripts/diagnostics/probe_amf2_case.tcl`、`audit_amf2_cases.py`：只读 Vivado 预检。
- `scripts/diagnostics/match_amf2_checkpoints.py`：跨阶段单元身份匹配。
- `scripts/diagnostics/finalize_amf2_cases.py`：生成最终索引和已匹配 DCP 的便利链接。
- `scripts/diagnostics/deduplicate_amf2_openpiton.py`：生成保留原始输入的逐字节重复记录去重副本。

预检目录：`experiments/preflight/20260930-amf2-case-preparation`、`20260930-amf2-case-audit`、`20260930-amf2-checkpoint-matching`。各阶段文件应结合 `manifest.json` 的最终状态读取。
