# 第一步：网表输入处理与初始分析评估

评估日期：2026-09-25。核对源码提交：`56397dbc278e226caf5233cb4487a0165267ccb0`。本次仅分析与记录，不修改布局器算法，也不重新运行布局布线。

结论：**AMF 2.0 的网表读取、基本连接图及初始拓扑分析可以复用，已在历史 faceDetect benchmark 上验证；课题方案第一步整体仍是“部分完成”，尚不能验收关闭。**

## 需求边界

依据 `docs/requirements/task-3-final.pdf` 第 13 页、1.2 节“支持增量编译的 FPGA 编译器”、图 6 后的（1）“网表输入处理与初始分析”：输入是经过逻辑综合与技术映射、由商用工具导出的网表，需构建包含逻辑单元、连接关系和时序约束的初始设计数据结构。

原文将多芯粒拓扑优化列在（2），增量控制器列在（3），完整轻量时序引擎列在（6）。因此，不应要求第一步已经实现全部增量算法、多芯粒优化和完整 STA。下文中的验收细则是针对本项目提出的工程拆分，不是原文逐项列出的考核条款。

主需求文件已经上传到服务器：

```text
/Projects/jinyang/workspace/AMFplacer3.0/docs/requirements/task-3-final.pdf
```

大小 1,231,875 字节；SHA-256 为 `451302ba0423ea3d2e5b71394861239ac6fee6a1d8141801fcffebfd24d7d67b`，与用户指定的本地原件一致。本次再次核验，未改写 PDF。

## 已有实现与证据

| 环节 | 当前实现 | 结论 |
| --- | --- | --- |
| 商用工具输入 | Vivado 打开 DCP，Tcl 导出单元、引脚、连接、时钟网络及宏/固定单元文件；AMF 读取导出的文本 ZIP | 通路已有，AMF 本身不直接解析 DCP |
| 逻辑单元和连接数据结构 | `DesignCell`、`DesignPin`、`DesignNet`；维护名称索引、驱动端和负载端、常量网络、单元类型分类 | 已有并在 benchmark 上运行 |
| 资源与控制信息 | 资源类型统计、FF 控制集、全局时钟网络与负载归属、预定义 cluster | 已有基本能力 |
| 初始拓扑分析 | 简化时序图、前向/后向层次化、最长拓扑路径长度 | 已运行，但不是完整约束驱动的签核 STA |
| 新 DCP 导出包的可用性 | 新导出的主网表能够解析，但外部宏与原生 CARRY 宏存在重叠，初始化流程失败 | 未完成 |
| 时序约束导入 | 时钟网络名单 + JSON 中的全局/按驱动网络配置的周期 | 部分完成，缺少从 DCP 统一导出和导入的约束链路 |
| 综合后未布局输入 | 当前完整验证使用已有 routed DCP 和匹配历史 benchmark；宏文件依赖 LOC/BEL | 未验证一般 post-synthesis/post-opt DCP 输入 |

成功实验 `faceDetect-benchmark-20260925-144131` 的 `logs/02_amf.log` 记录：

- 134,450 个输入逻辑单元，173,180 条 AMF 内部网络。
- 7 条被识别为全局时钟的网络，638 个 FF 控制集。
- 11,064 个预定义 cluster 引用单元；校验报告中缺失单元数为 0。
- 简化时序图已完成前向/后向层次化，日志记录 66 个层次。该图建立在初始打包之后，不能简单把其节点数等同原始输入单元数。

当前校验包括输入文件哈希、单元名称/类型匹配及 cluster/macro 引用存在性。它没有证明所有引脚连接、功能参数和时序约束与 DCP 语义完整一致，也不是功能等价验证。AMF 内部网络数量与 Vivado 的可布线网络统计口径不同，不应直接比较。

## 尚未完成的关键项

### 1. 新导出输入与宏读取约定不一致

`extractLUTRAMs.tcl` 除 LUTRAM 外还导出 ASYNC_REG、XPM_CDC 和 KEEP 对象；同一单元可被多次写入。`InitialPacker` 先构造原生 CARRY/MUX/BRAM/DSP 宏，再加载外部宏。

已有新导出实验的宏文件包含 1,179 行、797 个唯一单元，其中 52 个 `KEEP=yes` 的 FDRE 已属于 CARRY 宏，加载外部宏时再次归属，触发 `InitialPacker.cc:1531` 断言。仅去重仍失败；独立诊断中排除这些重叠对象仅证明可以通过该加载阶段，不能视作完整修复或功能约束验证。

另一个独立格式问题是读取器无条件跳过首行，但导出器没有写表头。历史宏文件有 54 行，日志加载 53 个外部宏单元。需定义明确的格式/版本和归属规则，不能为了通过实验而直接丢弃约束。

### 2. 时序网络识别不等于完整时序约束导入

`extractNetlist.tcl` 的 clocks 文件只写全局时钟网络驱动引脚，没有周期、波形、generated clock 关系或时序例外。`PlacementTimingInfo.cc:30` 从 JSON 读取 `ClockPeriod`，第 40 行起允许手工的 `ClockPeriod:<driver>` 覆盖。

本次 faceDetect 配置仅设置全局 `ClockPeriod = 15`。最终 DCP 的时钟报告包含多种实际周期，例如 20、10、8、4 和 2 ns，以及其他派生时钟。7 条全局时钟网络与报告中的 clock 对象也不是同一统计口径。

在本次检查的 AMF 应用、HiFPlacer 和 Vivado 导出脚本中，没有找到导入 XDC/SDC，或统一处理 generated clock、I/O delay、false path、multicycle path 的入口。不能据此宣称 AMF 已完整理解 DCP 时序约束。

Vivado 后端重新打开原 DCP，仍能使用其中原有的约束进行分析，所以后端 WNS 通过并不证明前端时序约束导入已完成。

### 3. 输入完整性诊断还需完善

现有解析器按空白分词，使用驱动引脚名归并网络；对空驱动字段有直接标记为未连接并跳过的分支。当前没有独立的输入诊断报告逐项说明端口、无驱动/多驱动、转义名称及不支持对象的处理结果。

需要明确支持的输入 DCP 阶段、器件与单元类型，区分合法特殊情况和真正输入错误；不能把名称/类型一致、没有崩溃当作网表语义完整性证明。

## 下一项具体工作与验收

建议先完成 **“可靠的新 DCP 输入包与只读分析入口”**，保留现有 `DesignInfo`，逐项补齐，而不是重写已有网表解析器。

1. 定义输入包版本、单元/引脚/网络字段、宏与固定对象的归属和表头规则。优先修复已定位的宏重复归属、首行丢失问题，保留约束来源与处理记录。
2. 从 Vivado 导出实际时钟及约束信息，并明确 AMF 第一版支持的约束子集；不支持的约束应显式标记或阻止其被当作完整时序输入，避免静默使用统一周期。
3. 提供独立 `import/analyze` 阶段：只加载和检查输入、建立基础图并输出机器可读报告，不必启动完整布局布线。
4. 以历史 benchmark、新导出的 faceDetect 输入和一个可控小设计作为回归；对综合后未布局 DCP 明确支持方式或限制，不能默认所有单元已有 LOC/BEL。

第一步关闭条件建议：新导出包可稳定加载；单元和引脚/网络连接与 Vivado 导出核对一致；宏无重复归属、无首行丢失；所声明支持的时钟/约束语义正确；不支持项有明确报告；分析结果与版本/输入哈希一起保存。无需等待完整增量布局器开发完毕。

面向后续增量编译，还需补充 LUT INIT 等功能参数、结构化约束、稳定身份映射、规范化快照和 V0/V1 差异。这些是后续 R1/R2 的实现内容，不应与 PDF 第一步的直接要求混为一项。

## 代码与实验定位

下列路径均相对服务器项目根目录；行号基于本报告核对的源码提交。

| 证据 | 位置 |
| --- | --- |
| 入口加载器件和网表 | `src/app/AMFPlacer/AMFPlacer.h:80` |
| 单元/引脚/网络解析 | `src/lib/HiFPlacer/designInfo/DesignInfo.cc:151` |
| 空驱动字段处理 | `src/lib/HiFPlacer/designInfo/DesignInfo.cc:210` |
| 时钟网络加载 | `src/lib/HiFPlacer/designInfo/DesignInfo.cc:370` |
| 单元资源统计 | `src/lib/HiFPlacer/designInfo/DesignInfo.cc:638` |
| 时钟周期配置、简化图构建 | `src/lib/HiFPlacer/placement/placementTiming/PlacementTimingInfo.cc:23`、`:80` |
| 宏加载次序、首行和冲突断言 | `src/lib/HiFPlacer/placement/packing/InitialPacker.cc:16`、`:1448`、`:1531` |
| 网表、全局时钟网络导出 | `benchmarks/vivadoScripts/extractNetlist.tcl:1` |
| KEEP/ASYNC_REG/XPM_CDC 宏导出 | `benchmarks/vivadoScripts/extractLUTRAMs.tcl:52` |
| 实验配置 | `experiments/runs/faceDetect-benchmark-20260925-144131/config.json` |
| 成功实验初始统计 | 同轮 `logs/02_amf.log`、`reports/input_validation.json` |
| DCP 实际时钟 | 同轮 `reports/timing_summary.rpt` 的 Clock Summary |
| 新导出失败及诊断 | `experiments/runs/faceDetect-20260925-131037/status.json`、`diagnostics/macro-conflict-134719/diagnosis.json` |
