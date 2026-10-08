# 按器件建立时钟资源容量表：首版实现与 U250 验证

> 后续核验更新：U250 数据库与 13/16/17 时钟微实验确认 SLICE 半列名义叶容量为 **16**；这里记录的首版 **12** 是 AMF/ISPD 兼容预算。新入口可用 `--query-half-columns` 自动读取，详见 [U250 半列数据库验证](u250-half-column-device-validation.md)。以下保留首版实现和当时实验记录，不把 12 当作已测得的 U250 硬件容量。

2026-10-04。正式实现位于 eda072 的 `/Projects/jinyang/workspace/AMFplacer3.0`。本轮在提交 `5ea1f494ce90ec71d0bcec10c06bf5e176316fd2` 的工作树上完成开发，尚未提交；独立构建保存了源码快照、源码哈希和工作树补丁。

AMF3 现在可以在 placement 前生成与具体器件及设备文件绑定的时钟容量表，并在加载器件时读取。区域覆盖预算和半列检查已经使用表中的值。首版经过实际验证的目标是 **`xcu250-figd2104-2L-e`，Vivado 2024.2**；规则白名单目前只开放 XCU250，其他器件需要增加规则并验证。

## 实际获得的容量

| 项目 | U250 结果 | 来源与含义 |
| --- | --- | --- |
| 时钟区域 | 128，8 × 16，分属 4 个 SLR | 实际器件查询，与 AMF 设备 ZIP 中的 site、CR、SLR 核对 |
| HROUTE：水平 routing tracks | 每区域 24 | 未布局探针设计的 `report_clock_utilization` 中 `Avail` 列 |
| HDISTR：水平 distribution tracks | 每区域 24 | 同上 |
| VROUTE：垂直 routing tracks | 每区域 24 | 同上 |
| VDISTR：垂直 distribution tracks | 每区域 24 | 同上 |
| 半列时钟上限 | 12 | 明确记录的 UltraScale placement 架构规则，非本轮 Vivado 直接查询的独立容量属性 |
| 区域 BBox 覆盖预算 | 24 | placement 模型规则，与四类物理轨道容量分别保存 |

四类轨道各 24 不表示一个区域可以覆盖 96 个独立时钟。同一个时钟可能使用多类资源；BBox 覆盖预算是 AMF 对区域时钟覆盖的模型。

SLICE 几何检查确认每个区域内的 SLICE 列有连续 60 行，按上下两半得到 7,200 个名义 SLICE 半列。这个数字仅覆盖 SLICE 几何，不表示已验证所有 DSP/BRAM/URAM 的详细时钟拓扑，也没有修改 AMF 原有半列映射。

轨道架构参考 [AMD UG572：Clock Structure](https://docs.amd.com/r/en-US/ug572-ultrascale-clocking/Clock-Structure)。半列 12 和矩形覆盖 24 的模型来源记录为 [ISPD 2017：Legalization](https://www.ispd.cc/contests/17/legalization.html)。直接读取值与规则值分别标记，避免将规则常数描述成 Vivado 查询结果。

## 实现接入点

| 文件 | 作用 |
| --- | --- |
| `scripts/export_clock_resources.tcl` | 查询 part/family、CR/SLR、时钟相关 site 和属性，生成布局前 clock utilization 报告；不可查询属性标记 unavailable |
| `scripts/export_fabric_device.tcl` | 新增 `--clock-resources`，可独立于 `--physical` 使用 |
| `configs/architecture/clock-resource-rules.json` | 器件白名单、四类轨道规则、半列与 BBox 规则、来源、版本 |
| `scripts/clock_resource_capacity.py` | 验证导出、生成 JSON/TSV、绑定设备 ZIP SHA-256、校验运行输入与二进制能力 |
| `scripts/amf3.py` | 新增 `prepare-clock-capacity`；构建记录实际二进制的 `--capabilities` 输出 |
| `src/lib/HiFPlacer/deviceInfo/ClockResourceCapacity.{h,cc}` | 严格解析容量 TSV，验证 part、规则版本、CR 完整覆盖和 SLR 对应 |
| `src/lib/HiFPlacer/deviceInfo/DeviceInfo.{h,cc}` | 在构造半列前加载每区域容量，将上限传入实际 `ClockColumn`，输出审计 JSON |
| `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc` | 区域覆盖检查和粗略半列检查从表读取阈值 |
| `scripts/inspect_amf_inputs.py`、`scripts/run_full_flow.py` | 启动前验证容量表与实际设备文件一致，拒绝不支持该功能的旧构建 |

生成物包含 `clock_capacity.tsv`（C++ 输入）、`clock_capacity.json`（来源与容量说明）、`config_overlay.json`（启用字段）、manifest、状态和原始查询记录。表中逐区域存储四类容量、SLR、半列上限及 BBox 上限；半列统一规则通过父区域传给现有半列对象。

若报告没有提供布局前逐区域轨道表，支持的器件可以采用显式架构规则，并记录 `architecture-rule-no-preplacement-track-table`。若提供了表但覆盖不完整、列序改变或值与规则冲突，则拒绝生成。解析读取 `Avail`，不会把 `Used` 或利用率误当容量。

旧配置保持显式 `legacy_24_12` 模式，物理轨道容量输出为未知。提供容量表后，错误不会静默退回旧常数。Python 入口验证实际 ZIP 的 SHA-256；裸 C++ 调用只检查元数据和 CR/SLR，因此报告明确写出 `archive_hash_validation_in_cpp=false`，正式运行应使用项目 Python 入口。

## 如何使用

以下命令在服务器项目根目录运行。生成一次表可以供相同器件、相同设备文件的多个 case 复用。

```bash
python3 scripts/amf3.py prepare-clock-capacity \
  --part xcu250-figd2104-2L-e \
  --device data/devices/u250-vivado-2024.2/exportSiteLocation.zip
```

命令会创建独立预检目录并输出路径。它用一个微型 OOC 综合探针打开器件数据库，不执行 placement 或 routing；这不改变任何 benchmark 的 OOC/完整设计设置。已有匹配原始导出时可通过 `--raw-dir` 复用，但仍重新检查设备文件身份和几何。外部 `--rules` 文件也会保存内容快照。

将生成的 `config_overlay.json` 合并到一个新的实验配置，新增两个字段：

```json
{
  "clock resource capacity file": "/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/clock-capacity-20261004-085542-241494/clock_capacity.tsv",
  "clock resource capacity part": "xcu250-figd2104-2L-e"
}
```

该示例表绑定标准 U250 设备 ZIP；CLK-FPGA08 使用的包含固定 I/O 资源的 ZIP 不同，已有另一张绑定正确哈希的表。不能仅因 part 相同而忽略 ZIP 身份检查。

本轮已准备好的 CLK-FPGA08 配置可直接进行输入检查：

```bash
python3 scripts/amf3.py inspect \
  --config experiments/evidence/clock-capacity-implementation-20261004/validation-clk08/clk08-u250-clock-capacity-config.json \
  --binary builds/build-20261004-085551-471666-5ea1f494/build/AMFPlacer
```

独立构建 ID 为 `build-20261004-085551-471666-5ea1f494`，AMF 二进制 SHA-256 为 `df58111ce1742da678b515f477ba4adb1a1ea072f3593a565e0d1d8f4310235c`。构建未切换 `builds/current`。启动器要求构建 manifest 声明容量 schema 1，并核对二进制哈希，防止旧 AMF 忽略新增 JSON 字段。

## 验证结果与证据

实际 Vivado 查询位于 `experiments/preflight/clock-capacity-20261004-084400-105155`，耗时约 80.6 秒。未执行 placement；128 个区域的四类轨道均报告 `Used=0, Avail=24`。其中 X4Y6 在 SLR1，四类容量各 24、半列规则 12、BBox 规则 24。

| 真实输入 | 单元数 | 网络数 | 时钟数 | 容量模式 | 输入检查耗时 |
| --- | ---: | ---: | ---: | --- | ---: |
| GETRF / U250 | 856,998 | 1,178,777 | 1 | device_table | 27.87 秒 |
| CLK-FPGA08 / U250 | 469,497 | 481,806 | 32 | device_table | 16.28 秒 |

两个 case 均核对了 AMF 输出中全部 128 个区域的四类容量、SLR、半列和 BBox 上限，与生成表完全一致。这证明加载与配置接入成功；输入检查不产生布局，也不能证明时钟可布通。

验证包括 15 项容量 Python 测试、30 项相关 flow 测试，以及 `checkClockResourceCapacity` 和 `checkClockCapacityDevice` 两个原生 C++ 测试程序。覆盖未知器件、错误 part/SLR/文件哈希、报告冲突或不完整、错误几何、旧二进制、逐区域不同容量、显式兼容模式和实际半列对象接入。构建快照的 3,417 个源码文件与测试时服务器源码哈希一致，`git diff --check` 通过。

第一次 DeviceInfo 测试因测试代码复用了会被构造函数修改的配置而失败；已将两个构造使用的配置分离，修正后通过。初次失败日志保留在 `validation-initial`，最终测试记录在 `validation-final`。这次修正未改变生产算法。

本地轻量证据镜像：

- [最终验证摘要](../../local-reports/clock-capacity-implementation-20261004/validation-summary.json)
- [标准 U250 容量与来源](../../local-reports/clock-capacity-implementation-20261004/tables/u250/clock_capacity.json) / [C++ 容量表](../../local-reports/clock-capacity-implementation-20261004/tables/u250/clock_capacity.tsv)
- [CLK-FPGA08 专用表与来源](../../local-reports/clock-capacity-implementation-20261004/tables/clk08/clock_capacity.json)
- [Vivado 布局前原始报告](../../local-reports/clock-capacity-implementation-20261004/probe/raw/preplacement_clock_utilization.rpt)
- [GETRF 实际读取结果](../../local-reports/clock-capacity-implementation-20261004/inspections/getrf/inputs.json) / [CLK-FPGA08 实际读取结果](../../local-reports/clock-capacity-implementation-20261004/inspections/clk08/inputs.json)
- [最终测试状态和命令](../../local-reports/clock-capacity-implementation-20261004/validation-final/test_results.json)
- [旧二进制拒绝记录](../../local-reports/clock-capacity-implementation-20261004/validation-final/old-binary-rejection.json)

标准 U250 表 SHA-256：`985f8c90b85b3d24e861abf338004937e25cd0383bb47f45c117cb4d3c3d5f19`。
CLK-FPGA08 表 SHA-256：`9ff0484cd2641ff9820106b65e0439e00762899bda4a8eb086c8912def1c1a15`。
完整运行记录保留在服务器；本地仅同步报告、日志和配置，没有下载 DCP。

## 本轮边界与后续接口

本轮实现的是名义容量建模，`design_occupancy` 和 `reserved_resources` 仍为 null，`clock_routability_verified=false`。四类物理轨道容量已经可读取、查询和审计，但尚未加入对应的轨道分配或冲突修复算法。

原有时钟覆盖软惩罚、半列几何映射以及最终非法结果的处理策略保持原状；CLK-FPGA08 之前的 X4Y6 覆盖超限不会仅靠这张表自动消失。下一阶段可据表计算每区域时钟覆盖需求，加入硬合法化与失败拦截，并对固定时钟资源预留量另建占用模型。这些应通过新的 placement/Vivado 对照实验验证，不能由本轮输入检查推断改善幅度。
