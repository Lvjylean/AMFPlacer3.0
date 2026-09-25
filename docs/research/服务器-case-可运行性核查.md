# 服务器 AMFPlacer 2.0 case 可运行性核查

日期：2026-09-25。服务器：`ssh eda072`；账号：`jinyang`；实际主机：`ee4e072`。

**最新状态：2026-09-25 14:52，整套历史 faceDetect benchmark 经隔离干净重编译后的 AMFPlacer 和 Vivado 2024.2 完成布局布线，路由错误为 0，WNS=0.300 ns、TNS=0。详见[完整验证记录](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/faceDetect-benchmark完整验证.md)。许可问题已解决；下文保留初始排查历史。DCP 新导出宏接口仍有独立的 KEEP 宏冲突待修。**

## 结论

服务器有可用于构建完整 DCP → AMFPlacer 2.0 → Vivado 布线流程的候选 case，并保留历史成功结果。推荐先用 faceDetect。**当前 jinyang 默认及登录 shell 环境下，Vivado 2024.2 实测缺少 Implementation / xcvu095 许可证，当前不能完成后端实现。**

本次实际进行了输入完整性检查、DCP 加载、单元集合比较和实现许可预检。没有重跑 AMFPlacer、完整布线或生成 bitstream；不能将本次结果记为端到端通过。

## 候选 case

| Case | AMF 网表单元数 | 配置引用输入 | DCP 情况 |
| --- | ---: | --- | --- |
| faceDetect | 134,450 | 11 项，均存在 | 已找到 opt、placed、routed DCP；routed DCP 本次成功加载 |
| digitRecognition | 265,775 | 11 项，均存在 | 已找到 opt、placed、routed DCP；本次未加载 |
| OpenPiton | 309,145 | 11 项，均存在 | jinyang 工程副本有 synth、opt DCP；本次未加载 |

AMF 配置和数据在 `/Projects/jinyang/AMF-Placer/benchmarks/`，已有二进制在 `/Projects/jinyang/AMF-Placer/build/AMFPlacer`。远端源码包含之前已记录的未提交改动，二进制与当前源码的构建对应关系尚未重新验证。

faceDetect 本次使用的输入为：

```text
/Projects/haoning/Vivado_Results/faceDetect_project/VCU108_PCIE_faceDetection/VCU108_PCIE_faceDetection.runs/impl_1/design_1_wrapper_routed.dcp
```

digitRecognition 的输入目录为：

```text
/Projects/haoning/Vivado_Results/digitRecog_project/VCU108_PCIE_digitRecognition/VCU108_PCIE_digitRecognition/VCU108_PCIE_digitRecognition.runs/impl_1
```

上述其他账号工程路径来自既有 jinyang 实验日志，本次确认 jinyang 有读取权限；没有改动原工程或 DCP。正式复现时应把选定输入保存为本项目的独立快照。

## 本次预检

- Vivado：`/Projects/Xilinx/Vivado/2024.2/bin/vivado`。
- 独立实验目录：`/Projects/jinyang/workspace/case-audit-4ylqb3l5`。
- `open_checkpoint` 成功，读取约 22 秒；DCP 来自 Vivado 2023.2。
- 器件：`xcvu095-ffva2104-2-e`。
- 当前 DCP 中 `IS_PRIMITIVE` 单元数：140,148。
- `IS_BLACKBOX` 单元数：0。
- 对内存中设计执行 `place_design -unplace` 时失败：`Common 17-345`，未找到 Implementation / xcvu095 有效许可证。
- 当前普通 SSH 环境及 `bash -lc` 环境中 `XILINXD_LICENSE_FILE`、`LM_LICENSE_FILE` 均未设置；用户 `.Xilinx` 目录未发现顶层 `.lic`。这只说明当前所测环境，未排除管理员维护的其他许可配置。
- 预检 Vivado 进程已退出，未留下运行中的编译任务。

日志本地副本：[case-audit-faceDetect-preflight.log](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/case-audit-faceDetect-preflight.log)。

## 输入一致性

历史 faceDetect AMF 输入包含 134,450 个单元，均能在当前加载的 DCP 原语集合中找到同名、同类型对象；DCP 集合额外有 5,698 个对象。当前统计使用的集合/规范化口径不完全相同，因此仅凭数量差异不能判定网表损坏或版本错误。

正式复现前需从选定 DCP 重新导出输入，并核对额外对象、引脚连线、固定位置和 LUTRAM/宏约束。不要把历史导出和当前 DCP 未经验证地拼接为同一实验输入。

## 历史成功证据

- `/Projects/jinyang/AMF_Results/AMF_origin_vivado2024/faceDetect_timing.rpt`：Vivado 2024.2，2026-03-29，Design State 为 Routed。
- 同目录 `faceDetect_vivado.log`：记录 AMF 布局 Tcl 导入及后续实现，最终输出 WNS=0.299 ns、TNS=0.000 ns；日志起止约 24 分 35 秒。属于历史结果，不能视为当前源码与二进制的性能承诺。
- `/Projects/jinyang/AMF_Results/AMF_origin_rerun/faceDetect/run.log`：`Placement Done (elapsed time: 415.942 s)`。它是另一份历史运行记录，不将其与上述后端耗时相加作为同一次端到端实验。

## 继续跑通的顺序

1. 配置管理员提供的有效 Vivado Implementation / xcvu095 许可，并重新验证。
2. 为 faceDetect 保存选定 DCP 的独立副本，从该 DCP 统一导出所有输入并检查集合一致性。
3. 保存 AMF 源码与用户现有修改的快照，确认二进制对应关系，以独立配置和输出目录运行。
4. 将新生成的布局 Tcl 导回同一设计，执行 place/route 并保存 routed DCP、DRC、路由状态和时序报告。
5. 如完整交付包含 bitstream，再检查相应生成条件并执行；本次未验证 bitstream 或上板。

## haoning 历史配置追溯

2026-09-25 后续只读核查发现：

- 系统账户数据库中的 haoning home 是 `/home/haoning`，shell 为 `/bin/bash`。jinyang 无法访问该 home，因此没有读取其中的启动配置、许可或历史记录，也没有尝试提权。
- 可读项目目录中的 `/Projects/haoning/.flexlmrc` 存在一行：`XILINXD_LICENSE_FILE=/tmp`。
- 本次 `/tmp` 和 `/var/tmp` 顶层未发现 `.lic`、`license.dat`、`licenses.dat` 文件。
- faceDetect 历史成功日志第 121、274429、274688 行均记录：`INFO: [Common 17-349] Got license for feature 'Implementation' and/or device 'xcvu095'`，分别对应 unplace、place、route。
- 历史入口 `route_isolated.tcl` 接收工程路径、布局 Tcl 路径、报告路径，执行 `open_project`、`open_run impl_1`，随后加载 AMF 布局 Tcl。可读副本仍在 `/Projects/haoning/amf_routing/route_isolated.tcl`。

这些证据确认历史运行成功取得了许可，并发现一个指向临时目录的许可搜索配置；尚不能证明该文件就是当时实际生效的配置。系统 home 和项目路径不同，当前环境也可能与 2026-03-29 不同。临时许可文件后来被清理是一种待核实解释，不是已证实原因。

要继续确认，需要由有权使用 haoning 账号的人在其原有 Vivado 终端查看有效 `HOME`、两个许可环境变量、`$HOME/.flexlmrc` 中的许可路径、`$HOME/.Xilinx` 下的文件名，以及 Vivado 的启动脚本。只需提供许可文件位置或许可服务器地址；无需提供许可证正文或密钥。

## 公共目录许可证核查

2026-09-25 根据用户提供的服务器根目录截图，补查可读公共目录 `/opt`、`/usr/local`、`/etc`、`/Projects/Xilinx`、`/Projects/temp`、`/tmp`、`/var/tmp`、`/srv`、`/mnt`、`/media`。使用有深度限制的文件名搜索，不进入私人账号目录，不跟随目录符号链接。共有 27 个目录不可读取，因此不将此结果表述为整台服务器绝对没有其他许可。

找到并核对的文件如下，未显示许可签名、密钥或完整正文：

| 文件 | 检查结果 | 能否解决当前缺少 Implementation 的问题 |
| --- | --- | --- |
| `/opt/licenses/rapidstream.lic` | JSON，字段为 `id` 和 `key`，未包含 Vivado/FlexNet 功能条目；`/Projects/rapidstream.lic` 同样是这类格式 | 无可用的 Vivado 实现授权证据 |
| `/Projects/Xilinx/Vivado/2024.2/data/ip/core_licenses/XilinxFree.lic` | 34 个 FEATURE/INCREMENT/PACKAGE 记录，内容为 XAUI、颜色转换、MAC 等 IP 功能 | 不包含 Implementation、xcvu095 或 Vivado 软件套件授权 |
| `/Projects/Xilinx/Vivado/2024.2/data/ip/core_licenses/Xilinx.lic` | 37 个 IP 功能记录，版本字段为 2024.11，例如 CMAC、Aurora、CAN、DisplayPort | 不包含 Implementation、xcvu095 或 Vivado 软件套件授权 |
| `/Projects/Xilinx/Vivado/2024.2/data/sysgen/hwcosim_compiler/pp_ethernet/Xilinx_IP.lic` | 3 个以太网 IP 功能记录 | 不包含所需实现授权 |

结论：目前可读取且已检查的公共路径中，没有找到能满足 Vivado 2024.2 对 xcvu095 布局布线需求的许可证。安装自带 IP 许可的版本较新或有效期为 permanent，并不等价于拥有 Vivado Implementation 授权。本次未发现需要重新做许可预检的有效候选，未更改任何账号许可配置。

## eda070：找到许可并通过实际验证

2026-09-25 按用户要求使用 `ssh eda070` 检查另一台服务器，实际主机名 `ee4e070`，登录用户 `jinyang`，home 为 `/Projects/jinyang`。本节结果只适用于 eda070，不能直接替代此前 eda072 的检查结论。

发现以下配置与文件：

- `/Projects/jinyang/.flexlmrc`：`XILINXD_LICENSE_FILE=/Projects/jinyang/.Xilinx`。
- `/Projects/jinyang/.Xilinx/xilinx_ise_vivado.lic`：可读，包含 `Vivado_System_Edition`，其 PACKAGE 组件包含 `Synthesis`、`Implementation` 等功能；版本字段为 `2037.05`，有效期字段为 `permanent`。
- `/Projects/jinyang/xilinx_ise_vivado.lic`：与上述文件内容一致的另一份副本。
- `/Projects/Xilinx/Vivado/2024.2/bin/vivado`：可执行。
- `.bashrc` 另引用 `/home/lduac/Software/Xilinx/Vivado/2023.2/settings64.sh`；本次明确使用 2024.2，不依赖该旧版入口。

为避免仅凭许可字段作判断，在独立目录 `/tmp/codex-vivado-license-check-dn1tdu5a` 中进行了实际验证。仅给这次进程设置 `XILINXD_LICENSE_FILE=/Projects/jinyang/.Xilinx/xilinx_ise_vivado.lic`，没有改写 shell 配置或复制许可到其他服务器。

测试对象为一个带时钟的单寄存器 Verilog 电路，目标器件 `xcvu095-ffva2104-2-e`。依次执行 `synth_design`、`place_design`、`route_design`，结果：

| 检查 | 结果 |
| --- | --- |
| Vivado 版本 | 2024.2 |
| Synthesis 许可获取 | 成功，日志第 16 行 |
| Implementation 许可获取 | 成功，日志第 185、369 行 |
| 综合、布局、布线 | 全部成功 |
| 路由错误 | 0 |
| 最终标记 | `AUDIT_VIVADO_2024_2_XCVU095_IMPLEMENTATION_OK` |
| 结束时间 | 服务器日志显示 2026-09-25 12:56:02 |

本地证据：[eda070-vivado-license-preflight.log](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/eda070-vivado-license-preflight.log)。

结论：eda070 上这份许可已被 Vivado 2024.2 实际接受，满足当前 xcvu095 的综合及布局布线许可需求。完整 AMFPlacer case 尚未在 eda070 重跑；这份许可在 eda072 上是否有效也尚未验证。本次没有改变 eda072 环境。

## 将许可复制到 eda072 并验证成功

按用户明确要求，将 eda070 的 `/Projects/jinyang/.Xilinx/xilinx_ise_vivado.lic` 通过 SSH 复制到 eda072 的同一路径。目标原先没有同名文件；使用原子落盘方式保存，文件权限为 `0600`，大小为 48,787 字节，两端 SHA-256 一致。未修改源文件、其他许可证或 shell 初始化配置，未在本地保存许可证正文。

在 eda072 新建独立验证目录 `/Projects/jinyang/workspace/license-check-8wm9jv94`，使用 Vivado 2024.2 默认许可搜索路径，未设置额外许可环境变量。打开既有 faceDetect routed DCP 后，执行内存中的 `place_design -unplace`：

- 器件：`xcvu095-ffva2104-2-e`。
- 日志第 70 行：`Got license for feature 'Implementation' and/or device 'xcvu095'`。
- 日志第 74 行：`AUDIT_IMPLEMENTATION_PREFLIGHT_OK`。
- 命令成功结束，0 Errors，进程正常退出。

本地验证日志：[eda072-vivado-license-after-copy.log](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/research/eda072-vivado-license-after-copy.log)。

此结果确认此前 eda072 的许可阻塞已经解除；它属于许可证及已有 DCP 的预检，不代表完整 AMFPlacer 布局布线流程已重新验证。
