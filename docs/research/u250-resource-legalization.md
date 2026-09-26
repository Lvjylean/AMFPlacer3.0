# U250 第二阶段：URAM 分配与专用级联合法化

当前范围是原始 GETRF 的硬资源分配，不使用外部 partition/floorplan、membership 或软 Pblock，不优化 SLL。不将本阶段成功解释为 U250 完整布局布线成功。

## 实现

- `URAM288` 和 `URAM288_BASE` 使用 U250 的 `URAM288` 站点与 `URAM_288K_INST` BEL；增加放置单元、聚类资源计数和资源栅格容量。独立 URAM 与 BRAM/DSP 共用宏合法化器的匹配、列分配和动态规划流程。
- URAM 排除在 CLB 打包和只适用于 CLB 的移动候选之外；接入硬资源位置输出。禁止使用被禁用或已占用的候选站点，最终输出再次检查唯一占位与资源类型。
- Carry/DSP 专用级联先检查宏是否完整且呈线性相邻关系，分配时检查站点同列、连续、同 SLR，输出前重新检查所有真实级联端点。普通数据连线不受同 SLR 限制。
- DSP 专用端口覆盖 AC/BC/PC、CARRYCASC 和 MULTSIGN；Carry 专用级联仅为 `CO[7] → CI`。DSP 宏的 Y 间距从器件数据库读取，避免沿用旧器件的坐标尺度。
- 在进入匹配前检查资源总容量与宏是否存在可容纳的连续段，尽早拒绝只有跨 SLR 站点可用的情况。原有 DSP/BRAM 宏不得跨 clock region 的更严格限制仍保留。
- GETRF 本轮使用独立 URAM；URAM 专用级联显式报错，未宣称支持 URAM cascade。

## 独立运行入口

服务器项目根目录执行：

```sh
python3 scripts/amf3.py legalize-resources \
  --config configs/experiments/getrf-u250-resources.json \
  --binary builds/<build-id>/build/AMFPlacer
```

入口读取完整网表，只执行 Carry/BRAM/DSP 相关初始宏识别、独立 URAM 分配与硬资源合法化（Carry 直接按初始位置分列，再执行列内匹配及精确动态规划，避免先建立全器件粗匹配大图）；不执行 MUX/LUTRAM/LUT-FF 打包、全局 CLB 布局或布线。默认用确定性的分散坐标初始化硬资源，避免把整个大设计挤在器件中心造成与实际使用无关的匹配热点；这不是拓扑优化或 module floorplan。支持可选 `resource initial locations file`，每行 `cell x y`，一个宏只指定一个单元。种子坐标只是该阶段的位置参考，不是用户 module floorplan。

随后在服务器执行 Vivado 回读验证：

```sh
python3 scripts/amf3.py validate-resources \
  --resource-run experiments/preflight/resource-legalization-<run-id>
```

每次运行保存输入与二进制哈希、构建来源、配置、退出状态、耗时、`resources.json`、`resources.tsv` 和 `place_resources.tcl`。Vivado 验证脚本 `scripts/validate_resource_placement.tcl` 回读原始 DCP、应用位置、逐单元核对 LOC/SLR，并生成仅硬资源已放置的部分 DCP；不代表完整 DRC、时序或布线通过。DCP 只留服务器。

## 回归与限制

集成测试 `tests/check_resource_legalization.py` 使用真实 U250 器件数据，覆盖 URAM 类型、竞争、固定占位、禁用站点、容量不足、普通数据以及 Carry 的 O/非末级 CO 输出跨 SLR、Carry/DSP 接缝避让、只剩非法跨缝候选、MULTSIGN 级联、URAM cascade 拒绝，以及旧 VCU108 的 Carry/DSP 合法化。

GETRF 的旧完整打包路径另有 MUX 兼容问题：`InitialPacker::findMuxMacros` 假定 MUXF7 数据输入驱动一定为 LUT，实际网表触发断言。本阶段通过明确的硬资源模式避免调用该 CLB 打包路径；问题本身保留为下一阶段必须解决的事项。

完整 U250 入口继续保留保护。后续还要完成全局初始化/聚类资源使用、URAM 时序建模、MUX/CLB 打包兼容、完整 U250 放置结果回灌、Vivado 布线、DRC 与时序验证。当前没有性能或时序质量结论。


## Carry 端口判定

GETRF 有 10,939 条 Carry 单元到 Carry CI 的连接，其中 36 条是 `O[5] → CI` 的普通数据连接，真正的 `CO[7] → CI` 专用级联为 10,903 对。旧打包器只检查接收端 CI 和驱动 cell 类型，会把这 36 条普通连接也合成物理链；本次让初始打包和边界检查共用相同的端口判定，并增加跨 SLR 回归。依据 [AMD UG574 的 Carry 引脚说明](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/Pin-Signals?contentId=SpzZqSkDj91EbkfETi5G4Q)。


## GETRF 验证记录（2026-09-26）

- 构建：`builds/build-20260926-143505-263463-35e9dcfd/build/AMFPlacer`；源码快照与提交前 C++ 文件逐个 SHA-256 相符。
- 资源运行：`experiments/preflight/resource-legalization-20260926-143704-946049`，退出 0，入口总耗时 74.985 秒（包含器件、网表加载和初始宏识别；不是完整 placer 耗时）。
- 合法位置共 26,096 个：URAM288 256、DSP48E2 2,816、RAMB36E2 515、RAMB18E2 1、CARRY8 22,508。URAM 在 SLR0–3 的分配为 62、65、65、64；本阶段不评价这一分布的时序或连线质量。
- 检查了 10,903 对 Carry 专用级联和 1,280 对 DSP 专用级联；无跨 SLR、非连续站点或重复占位。
- 15 项 C++ 资源集成测试：`experiments/preflight/resource-checks-20260926-1436`。另有 6 项输入回归（`input-regression-20260926-1420`）及 `make check` 的 21 项 Python 测试通过。
- 早期编译失败、中心初始化/全器件 Carry 粗匹配诊断均保留。中止运行记录原因，不计为成功或性能对照。独立资源入口最终采用分散初始坐标与 Carry 直接分列。

- Vivado 2024.2 回读运行：`experiments/runs/getrf-u250-resources-20260926-143933-202523`，退出 0，26,096 个单元的实际 LOC/SLR 全部与 AMF 输出一致，256 个 URAM BEL 均正确。原始 DCP 打开 75.305 秒，位置导入 30.302 秒，审计/时钟报告/DCP 写出 67.712 秒，含工具启动退出的验证总耗时 179.533 秒。
- 部分布局 DCP（仅服务器）：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-resources-20260926-143933-202523/reports/getrf_hard_resources_partial.dcp`。
- DCP SHA-256：`6933a477ebd71217672f552988f90e25dd4b9176507bcc258ac80ad88d7e5709`。没有执行 CLB 全量布局、布线、完整 DRC 或时序收敛验收。
- 第一次 Vivado 审计成功导入位置，但审计脚本未处理 `URAM288.URAM_288K_INST` 的类型前缀而退出 2；修正属性解析后以最终源码对应位置重新完成回读。失败记录保留于 `getrf-u250-resources-20260926-143230-527023`，没有把该次当作验证通过。
