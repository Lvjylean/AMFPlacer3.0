# AMF3 DFX 增量编译：实现与验收

日期：2026-10-13。启用参数按用户指定拼写保留为 `IncremenralCompilation`，默认关闭。

## 需求与基线

本次增量编译指 Dynamic Function eXchange（DFX）：用户在 RTL module/实例上以 `bblock` 标记可替换模块，首个配置建立硬 Pblock 与静态区，后续仅重新实现活动 RM，复用静态布局、布线和分区接口，生成完整与局部 bitstream。不是按网表差分移动少量 cell 的通用 ECO。

开发副本（eda072）：

```
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo
```

基于 eda099 确定性匹配副本提交 `4dcfefded476cb9571e43e3edb4ff0d7830b8ed2` 的选定源码归档。保留网格 batched 更新、`legacy_components` 子图独立求解、普通 Pro、U250 坐标与距离系数。`ProPrimitiveTiming=false`。源码归档与逐文件哈希见副本父目录 `copy-manifest.json`。这是独立导入仓库，不伪称保留上游全部 Git 祖先历史；开发分支为 `codex/dfx-incremental-compilation`。

参考用户的 [FPGA-DFX-HLS-Project](https://github.com/Lvjylean/FPGA-DFX-HLS-Project/tree/34669f8888e3800c0b8d6c1206892c39cf3b17dd)。复用其锁定静态区、替换 RM、`pr_verify` 和双 bitstream 思路；不采用降级 DRC 的处理。

## 分步实现

| 步骤 | 实现与验收条件 |
| --- | --- |
| 1. 建立首个配置 | `dfx-shell` 综合后识别一个 `bblock` 实例，设置 HD.RECONFIGURABLE 和硬 Pblock，Vivado 完成首轮布局布线及双 bitstream；black-box 活动 RP 后 `lock_design -level routing` 保存静态 DCP。首轮静态配置由 Vivado 实现。 |
| 2. 准备活动模块 | `dfx-run` 加载锁定静态 DCP，导入 RM 的 OOC DCP，验证器件、时钟、Pblock、接口名称/方向，检查静态 primitive 及 LOC/BEL 不变。 |
| 3. AMF 区域/冻结约束 | 从 Vivado 实际 Pblock site 列表与 AMF 模型取交集；Pblock 外、PROHIBIT 和静态占用 site 均不可用于活动 RM。静态 fabric cell 作为固定、锁定的单 cell PU 保留网络与坐标上下文；其资源不重复计入活动需求。 |
| 4. 活动 RM 布局 | 在区域内确定性初始化，执行现有 GP、合法化、打包与细布局。静态锚点在导出前复核。仅导出活动 RM 的放置数据，严格检查覆盖和 site 范围。 |
| 5. Vivado 后端 | 将 AMF 数据逐 cell 导入 linked DCP，不执行全设计 unplace。单条放置命令失败即终止；允许 Vivado 对活动 RM 自动重映射 LUT，但导入后必须仍位于可用 RP site，并逐 cell 记录导入重映射。place/route 后分别审计静态位置、时序、DRC、RM 位置保留。执行 `pr_verify -full_check`，再生成完整与局部 bitstream。 |
| 6. 分级扩大验收 | 先完成用户选定的 U250 小型设计；其后才扩展到含 SRL/CARRY/DSP/RAM 的 RM 和真实 HLS 模块，验证复杂资源、时钟与边界预算。 |

## 命令与参数

标记示例（同时保留层次）：

```verilog
(* bblock = "true", keep_hierarchy = "yes" *)
module rm(input wire clk, input wire [7:0] data_in,
          output reg [7:0] data_out);
// ...
endmodule
```

首个 shell 的 job 提供 `rtl_sources`、`xdc_files`、`top`、`part`、`pblock`、`pblock_ranges`、`clock_name`、`clock_period_ns`、`vivado`；综合后解析属性，不以正则猜测 Verilog 层次。替换 job 提供 `static_checkpoint`、`reference_checkpoint`、`rm_checkpoint`、`active_partition`、AMF 模板和相同时钟/器件信息。路径相对各 job 文件解析，输出目录不可复用。

```sh
python3 scripts/amf3.py dfx-shell --config configs/experiments/dfx-u250-shell.json --output experiments/runs/<new-shell-id>
python3 scripts/amf3.py dfx-run --config configs/experiments/dfx-u250-replace-b-verified.json --output experiments/runs/<new-rm-id> --binary builds/<frozen-build>/build/AMFPlacer
```

后端失败时可直接复用记录中 exit 0 的 AMF 结果；续跑创建新的目录并记录父轮次、linked DCP/放置数据哈希，不覆盖父轮次：

```sh
python3 scripts/amf3.py dfx-resume --run experiments/runs/<amf-completed-id> --output experiments/runs/<new-backend-id>
```

两种 job 均必须显式设置 `"IncremenralCompilation": true`。C++ AMF 配置以现有字符串配置约定写入 `"IncremenralCompilation": "true"`。普通全流程入口拒绝误用该开关，避免其既有全设计清除位置流程破坏静态区。

模板不得携带 GETRF 专属 fixed units、designCluster、外部 floorplan 或原设计控制元数据。SRL 控制元数据从本轮 linked DCP 重新提取。用户指定 10 ns 同时用于 AMF 目标配置及 Vivado DCP 校验；普通 Pro 既有内部搜索目标收紧仍保留，不能将该内部数值误认为后端 XDC 改变。

## 小型设计与当前适用范围

`tests/dfx/u250_small/` 包含静态 JTAG/BUFG、寄存器接口及两个可替换 RM。两版 RM 端口一致，逻辑与优化后 primitive 数量不同。无需板级 I/O 分配；没有下载或烧写硬件。

首版是**一个活动 fabric RP**；任意多 RP、嵌套 RP、多时钟 HLS 或硬 IP 的全覆盖验收尚未完成。未验证类型不能据小型 LUT/FF 验收声称已支持。实际合法资源采用离散 site 清单；GP 连续坐标约束采用其包围盒，非矩形空洞由资源容量/候选与最终导出范围检查阻止非法落位。静态占用 site 整体保留，暂不与 RM 共享其中剩余 BEL。

接口与时序有三层处理：保留 RTL 端口及分区引脚信息；AMF 保留固定静态 fabric 的坐标/连通性参与原 STA；Vivado 在完整锁定静态上下文中进行最终 setup/hold 与跨接口路径分析。**尚未将锁定布线的精确 net delay 或逐 partition pin arrival/required budget 回注 AMF**，不能把 AMF 估计等同 Vivado 边界时序。非 fabric 静态数据驱动或接收端直接连接 RM 时当前显式拒绝，避免静默丢失数据路径；专用时钟保留在 Vivado 上下文中。

完整静态 fabric 上下文仍参与 AMF 数据加载与时序分析，首版尚未证明大规模增量编译的速度收益。小设计成功也不等于硬件动态重配置协议、隔离/复位逻辑或 HLS RM 的系统功能已经验证。

## 证据与计时

每轮 manifest 记录源码提交、工作构建及二进制哈希、输入/模型/脚本哈希、命令、阶段墙钟和退出状态。运行完成、全量布线、DRC、静态保持、接口兼容、时序通过和 bitstream 生成分别核验。AMF 进程时间包含器件/网表加载等开销；细粒度 profile 单列，不能统称纯布局算法时间。

最终验收数字、构建与产物路径见同目录 `dfx-u250-validation.md`。DCP、bitstream 仅留服务器；本地保存轻量源码、配置、日志和报告。

## 后续阶段的完成标准

1. **边界时序模型**：提取锁定静态路径到 partition pin 的到达时间和剩余 required budget，区分数据 net/cell delay 与时钟/约束；用 Vivado 路径级报告交叉验证，再决定是否替代当前固定静态 fabric 的估计。不能直接把整条路径 slack 当连接延迟。
2. **资源覆盖**：增加 SRL 级联、CARRY、BRAM/URAM、DSP 注册/旁路输出的模块替换测试，复核宏边界、连续 site、静态占用和专用级联；通过前不把 LUT/FF 样例推广到 HLS 全资源支持。
3. **约束/兼容性回归**：端口宽度或方向改变、Pblock 不兼容、器件或时钟不一致应拒绝；无变化 RM 与连接变化 RM 各自保存证据，多 RP 另行设计。
4. **性能验收**：以同一个替换后的设计比较全量与静态复用流程，分别记录准备、AMF、导入、Vivado place/route、审计和 bitstream 时间；大设计才评估区域化数据加载/STA 是否值得实现。
