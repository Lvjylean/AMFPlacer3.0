# U250 适配：输入与器件模型

> 本页记录2026-09-26输入移植阶段的历史行为与复现命令；其中RPM模式现在需显式传入 `--x-model rpm`。当前U250列构造规则与使用配置以 `coordinate-unification.md` 为准。

2026-09-26，开发分支 `codex/u250-uram`。本阶段实现 U250 器件数据和 URAM 网表读取；完整 U250 布局、URAM 合法化和布线验证仍待后续阶段完成。

## 当前范围

目标器件 `xcu250-figd2104-2L-e`，输入为原始 GETRF post-opt DCP。不读取 partition、membership、floorplan 或软 Pblock，不引入 SLL 优化。原始输入与级联统计见 [范围说明](u250-getrf-porting-scope.md)。

源码、构建、原始 DCP 和生成的器件/网表 ZIP 均保存在 eda072 的 `/Projects/jinyang/workspace/AMFplacer3.0`。本地仅保留脚本、文档和轻量证据。

## 已实现的接口

- `DeviceSite` 保存 SLR ID；器件文件增加可选尾字段 `slr=> N prohibited=> 0|1`。现有受支持的 VCU108 文件省略该字段时仍按 SLR0 读取。
- `export_fabric_device.tcl` 查询实际 SLICE、DSP、BRAM、URAM 站点、BEL、时钟区域和 SLR。支持从 DCP 或通过 `--part` 加载空器件后导出。空器件模式的可用性信息不包含设计专属的 PROHIBIT、LOC 或 Pblock，不能替代后续约束导入。
- `prepare_fabric_device.py` 校验器件型号、重复站点、资源行连续性和时钟区域的 SLR 归属，生成 ZIP 与哈希清单。X 保留 RPM 列间距比例；Y 按时钟区域中的资源行数归一化。这是布局度量坐标，尚未校准延迟模型。
- 网表枚举新增 `URAM288` 和 `URAM288_BASE`，追加在已有枚举尾部，保持原编号稳定；归入硬资源/时序端点分类。
- `convert_netlist_inventory.py` 将绑定原始 DCP 的 cells/nets 清单转换为 AMF 格式，保留名称和引脚连接，折叠常量单元，保留外部时钟驱动身份。支持高扇出长行和 ZIP64；不导入 floorplan。原始划分清单省略了时钟负载，必须先用 `diagnostics/export_external_clock_pins.tcl` 从同一 DCP 导出完整时钟引脚，再通过 `--clock-pins` 补齐；哈希不一致、连接冲突或时钟无负载都会被拒绝。
- 新增 `amf3.py inspect`：调用真正的 C++ 器件和网表读取器，输出各 SLR 资源、单元类型、网络、时钟负载和 URAM 容量检查结果，拒绝配置中存在但没有实际连接的时钟，保留二进制/输入哈希、构建来源、配置、日志、耗时和退出码。
- 设计没有 PCIe 时，不再强制要求 VCU108 的 PCIe 站点与引脚偏移表。常量正则改为每次读网表编译一次，避免逐引脚重复编译。

当前完整布局入口对多 SLR 或含 URAM 的输入明确报错。只有输入检查入口已经开放，不能将成功读取等同于布局兼容。

## 重现入口

以下命令在服务器项目根目录运行，输出路径每轮必须新建：

```bash
python3 scripts/amf3.py build --jobs 8
/Projects/Xilinx/Vivado/2024.2/bin/vivado -mode batch \
  -source scripts/export_fabric_device.tcl \
  -tclargs --part xcu250-figd2104-2L-e <new-device-directory>
python3 scripts/prepare_fabric_device.py \
  <new-device-directory>/sites.tsv <new-device.zip> \
  --part xcu250-figd2104-2L-e --metadata <new-device-directory>/metadata.tsv --x-model rpm
python3 scripts/amf3.py inspect --config <input-config.json> --binary <build>/AMFPlacer
```

输入检查配置只需 `device`、两个 `vivado extracted ... information file` 路径、可选 `clock file` 和 `jobs`；不依赖布局资源映射表，也不设置或推断目标周期。GETRF 已有 `ap_clk=10 ns` 仅作为原始约束记录，后续 placer 目标仍以用户配置为准。

独立 C++ 回归入口为 `tests/check_amf_input_binary.py`，参数包括 `--binary`、`--legacy-device`、`--u250-device`、`--output`。旧器件应使用成功 faceDetect 实验的 `inputs/baseline/exportSiteLocation.zip`；上游 `benchmarks/VCU108/preprocessPython/` 中另有缺少时钟区域字段的历史文件，并非当前程序支持的基线输入。

## 验证记录

主目录：`experiments/preflight/u250-input-stage-20260926-01/`。最终检查轮次为 `experiments/preflight/input-inspection-20260926-134213-200033/`，退出码 0。

| 项目 | 最终结果 |
|---|---:|
| SLR | 4 |
| 导出的 fabric 站点 | 237,632 |
| URAM 站点 / GETRF URAM 单元 | 1,280 / 256 |
| 非恒值逻辑单元 | 856,998 |
| AMF 网络 | 1,178,777 |
| AMF 引脚 | 5,751,497 |
| 时钟 | 1，`@PORT/ap_clk` |
| 时钟负载单元 / 输入引脚 | 407,203 / 407,718 |
| C++ 输入检查耗时 | 27.87 秒 |
| 项目测试 / C++ 集成检查 | 21 / 6，全通过 |

上述耗时只覆盖 C++ 输入读取与检查，不是 placement 或完整编译耗时。尚未执行 U250 布局、合法化和布线，也没有本阶段最终 routed DCP。

已验证器件库放在 `data/devices/u250-vivado-2024.2/`，GETRF 网表在 `data/reference/getrf-u250/amf-inputs/`；晋升前后文件哈希一致。复现本阶段：

```bash
python3 scripts/amf3.py inspect \
  --config configs/experiments/getrf-u250-input.json \
  --binary builds/validated-u250-input/AMFPlacer
```

固定构建为 `builds/build-20260926-133959-149850-4d324dfd/`，包含源码快照、源文件哈希、基础提交与工作区补丁。`validated-u250-input` 仅表示输入层验证通过。

已修复并保留失败证据：CSV 单字段 128 KiB 限制、ZIP 单文件 2 GiB 限制。原始 DCP 从 eda070 直接流式传到 eda072，实际 SHA-256 与绑定记录一致：`6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031`。

原始清单的时钟负载缺失在首次 C++ 汇总报告中被发现（`clock_count=0`），该轮已标记为不接受，不能作为完整输入验证。补全时钟后另建 ZIP 与检查轮次，保留原文件追溯。

器件导出的增长字典造成非线性耗时，替换为 Tcl 数组后，空器件导出全量 237,632 个相关站点耗时 55.73 秒。使用相同优化脚本从 GETRF DCP 导出的 `sites.tsv` 与空器件版本 SHA-256 完全一致，包含 PROHIBIT 字段，因此本 GETRF 可以复用该器件数据。

在 Vivado 2024.2 中重新打开 DCP，非恒值逻辑单元共 856,998 个；与原始清单比较，名称增减和类型变化均为 0。该检查不等同于所有普通网络连接的跨版本一致性证明。

## 下一阶段验收条件

1. URAM 进入资源映射、密度/容量模型、PlacementUnit、独立资源合法化和位置 Tcl 输出；不能只在原语枚举中增加名称。
2. Carry/DSP/BRAM 专用级联候选需校验站点连续性和 SLR 边界。普通网络允许跨 SLR，不能把任意逻辑分组一概限制在单 SLR。
3. 检查 DSP 宏高度、时钟半列、资源坐标、硬编码 VCU108 时序惩罚等既有假设。当前统一二维坐标不代表时序模型已适配。
4. 从小设计逐步进入 GETRF 全量布局；Vivado 检查 URAM LOC/BEL、布局合法性、路由完成度、时序和 AMF 位置保留率。最终 DCP 仅留在服务器。
5. 本 GETRF 没有已知 URAM cascade，独立 URAM 成功不代表任意 URAM 级联已支持；跨版本网表完整连接一致性仍需在正式后端流程中核验。
