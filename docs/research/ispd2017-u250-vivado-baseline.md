# CLK-FPGA08 / U250：Vivado 原生布局布线

2026-09-29 用户授权让 Vivado 自行完成全量布局布线，验证 CLK-FPGA08 能否在 U250 上实现。此前 r10 参数的 AMF 布局虽能完整导入，仍在时钟路由阶段失败，不能据此判断整个设计不可实现。

## 实验配置

- 服务器：`eda072`，项目根目录 `/Projects/jinyang/workspace/AMFplacer3.0`。
- 运行：`experiments/runs/clk-fpga08-u250-vivado-10ns-20260929-194539-494632`。
- 对照输入来源：`clk-fpga08-u250-r10-10ns-30clocks-full-20260929-192441-680361`。
- 输入 DCP：`experiments/preflight/20260929-clk-fpga08-timing-correction-192214/corrected/amf_input_30clocks.dcp`。
- 输入 SHA-256：`0a39ec53f2620097de6e58b799eb2c32b1afb1671f43821438339ad117693fa2`，与 AMF 后端对照相同。
- 器件：`xcu250-figd2104-2L-e`；Vivado 2024.2；最多 4 个线程。
- 时钟：30 个真实时钟，均为 10 ns；另有 2 个使用 BUFG 的全局复位/使能网络。
- 保留输入 DCP 的时序约束、I/O 封装引脚及 IOSTANDARD；解除单元 LOC/BEL 固定，清空布局并释放 BUFG 的位置。无 AMF 位置导入。
- 执行默认 `place_design` → `route_design`。本轮没有额外执行 `opt_design`、`phys_opt_design`，以便观察同一输入网表的原生布局效果。
- 最终 DCP 仅保存在服务器；本地同步报告、日志、参数与指标。

此实验用于器件级布局布线验证，输入的自动 I/O 分配尚未对应 U250 板级接口或平台 shell。10 ns 是用户指定的本轮目标，原始 benchmark 没有该周期约束。

## 固定 I/O 主实验结果

主实验于 2026-09-29 21:08（香港时间）完成。结论：同一输入网表、同一组 332 个 I/O 引脚和 30 个 10 ns 时钟条件下，Vivado 已在 U250 上完成全部网络的布局布线；建立时间通过，保持时间未收敛。这证明先前 AMF 布局的时钟区域容量失败并不意味着该算例在 U250 上无法布通，仍不能宣称当前结果已通过全部时序验收。

| 项目 | 布线后结果 |
|---|---:|
| 可布线 / 完整布线网络 | 398,083 / 398,083 |
| 路由错误网络 | 0 |
| DRC Error / Critical Warning | 0 / 0 |
| DRC Warning / Advisory | 388 / 450 |
| WNS / TNS | +0.832 ns / 0 ns |
| Setup 违例端点 | 0 / 265,397 |
| WHS / THS | −2.834 ns / −12,023.301 ns |
| Hold 违例端点 | 16,354 / 265,397 |
| WPWS / Pulse width 违例端点 | +4.458 ns / 0 |
| `place_design` | 1,906.389 s（31.773 min） |
| `route_design` | 2,783.582 s（46.393 min） |

上述时序来自最终 `report_timing_summary`，不是路由器的中间估计。布线后规范单元总数仍为 469,501（含 4 个 GND/VCC）。Vivado 布局过程中将 1 个 LUT6 映射为 LUT4，其他类型计数不变；未额外运行 `opt_design`。`summary.json` 中的 `implementation_verified=true` 仅表示完整布线且 DRC 无 Error，完整时序结果另由 `timing_met=false` 表示。

最差 hold 路径为 `FDRE_inst_bmezl/C`（clk9）到 `FDRE_inst_bvzmv/D`（clk1）：数据路径延迟 0.171 ns，时钟偏差 2.538 ns，WHS −2.834 ns。两端时钟均为 10 ns、同相位，本次模型下 hold 边沿间隔为 0 ns。日志 `Route 35-514` 说明保持时间违例过多导致路由器停止自动 hold 修复，另有 `Route 35-455`、`Route 35-459`。前几条最差路径均跨时钟域，不能在缺乏设计时钟关系的情况下直接添加 asynchronous / false-path 约束来消除报告。

内部端点检查 `no_clock=0`、`unconstrained_internal_endpoints=0`。仍有 201 个输入未设置输入延迟、100 个输出未设置输出延迟，因此本轮建立时间结果仅针对现有已约束路径，不代表板级 I/O 时序通过。

### 真实多 SLR 使用

最终 `utilization.rpt` 显示逻辑跨 SLR0 和 SLR1，SLL 共使用 2,024 条：SLR0 → SLR1 为 1,039，SLR1 → SLR0 为 885，SLR1 → SLR2 为 100。SLR2、SLR3 没有放置 LUT/FF/BRAM/DSP，但有 I/O。

| 资源 | SLR0 | SLR1 | SLR2 | SLR3 |
|---|---:|---:|---:|---:|
| CLB LUTs（物理 LUT 占用） | 17,515 | 170,023 | 0 | 0 |
| CLB Registers | 23,906 | 232,742 | 0 | 0 |
| RAMB36/FIFO | 15 | 146 | 0 | 0 |
| DSPs | 8 | 67 | 0 | 0 |
| IOBs | 0 | 208 | 112 | 12 |

最终时钟路由资源表中，128 个 CR 的 HROUTES/HDISTRS/VROUTES/VDISTRS 均未超过各自容量 24，四类资源的全器件峰值分别为 8、19、11、10；X4Y6 分别为 4、8、2、4。该表统计实际路由轨道占用，不能与 AMF 失败布局中按时钟负载覆盖矩形得到的 32 直接相减。

### 最终 DCP

服务器路径：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/clk-fpga08-u250-vivado-10ns-20260929-194539-494632/reports/vivado_routed.dcp
```

SHA-256：`7dc4819d18418c6475e88cef0ddbceca920b2293c3c223e2e766a0eeb01ab6e4`。最终 DCP 未下载到本地，也未生成板级 bitstream。

## 准备阶段的修正

首次准备运行 `clk-fpga08-u250-vivado-10ns-20260929-194232-038699` 在进入正式布局前被一致性检查中止：`place_design -unplace` 同时清空了自动分配的 PACKAGE_PIN。修正脚本在清空布局后恢复逐端口保存的封装引脚与 IOSTANDARD，并验证时钟和 I/O 条件与输入一致；本轮恢复检查已通过。该准备失败不属于设计可布线性的失败。

## 可复现入口

在服务器项目根目录运行：

```sh
python3 scripts/amf3.py vivado-run \
  --reference-run clk-fpga08-u250-r10-10ns-30clocks-full-20260929-192441-680361 \
  --run-prefix clk-fpga08-u250-vivado-10ns
```

入口自动生成唯一运行目录，验证参考输入哈希，快照脚本、源码提交与工作区差异，记录 Vivado 阶段耗时和退出状态。失败阶段也记录独立耗时。`--opt-design` 仅在明确选择时增加逻辑优化；本轮未使用。

关键结果文件：`status.json`、`manifest.json`、`reports/summary.json`、`reports/stages.tsv`、`reports/route_status.rpt`、`reports/drc.rpt`、`reports/timing_summary.rpt`、`reports/clock_utilization.rpt`。若生成最终检查点，其路径为 `reports/vivado_routed.dcp`。

## 自动 I/O 分配对照

固定 I/O 的原生运行在 `Clock Region Placement` 中耗时较长，随后成功完成布局。其 `clockInfo.txt` 显示 32 个 BUFG 源位于 X4Y6–X4Y14，初始负载矩形大量覆盖下方区域；预分配的 I/O 位置可能限制源位置选择，但不能仅凭这一点断言它是失败原因。

为检查这种限制，增加独立运行 `clk-fpga08-u250-vivado-free-io-10ns-20260929-200515-458636`，通过上述入口加 `--release-io`，并使用前缀 `clk-fpga08-u250-vivado-free-io-10ns`。该轮释放自动 PACKAGE_PIN，保留逐端口 IOSTANDARD、同一输入网表、30 个真实时钟及 10 ns 约束；同样没有额外逻辑优化。两轮并行运行，因此不用于严格的运行速度比较。输入和最终的端口分配分别记录于 `input_ports.tsv`、`final_ports.tsv`，其结果与固定 I/O 轮分别验收。

该补充轮于 **2026-09-29 21:57:18（香港时间）完成**，2026-09-29 22:06 复核并同步最终报告。全部 398,076 个可布线网络完整布通，路由错误为 0，最终规范单元总数 469,501；setup 通过，hold 未通过。此前 `Route 35-447` 拥塞警告和中间冲突数不再作为最终结果。

| 项目 | 固定 I/O 主实验 | 释放自动 I/O 对照 |
|---|---:|---:|
| 完整布线 / 可布线网络 | 398,083 / 398,083 | 398,076 / 398,076 |
| 路由错误网络 | 0 | 0 |
| DRC Error / Critical Warning | 0 / 0 | 0 / 1（UCIO-1） |
| WNS / TNS | +0.832 ns / 0 ns | +0.250 ns / 0 ns |
| WHS / THS | −2.834 ns / −12,023.301 ns | −2.973 ns / −14,444.025 ns |
| Hold 违例端点 | 16,354 | 14,586 |
| WPWS / Pulse width 违例端点 | +4.458 ns / 0 | +4.458 ns / 0 |
| `place_design` | 1,906.389 s（31.773 min） | 1,926.360 s（32.106 min） |
| `route_design` | 2,783.582 s（46.393 min） | 4,541.345 s（75.689 min） |
| SLL 使用量 | 2,024 | 3,903 |

两轮逻辑网统计均为 529,663。自由 I/O 轮的内部已连接网络比主实验多 7 个，对应需要外部路由的网络少 7 个，不代表减少了 7 条逻辑网。两轮并行期间共享服务器，阶段耗时仅作为本轮观测值，不用于严格速度比较。

`UCIO-1` 的原文指出：332 / 332 个逻辑端口没有用户指定的 LOC 约束。Vivado 已自动为它们选择实际引脚，但没有用户固定的板级引脚约束；应在生成板级 bitstream 前完成接口和引脚适配。未降低该检查的严重级别。逐端口比较 `input_ports.tsv` 与 `final_ports.tsv`：332 个最终 PACKAGE_PIN 均非空，**引脚变化 0、IOSTANDARD 变化 0**。本轮释放的是分配约束，最终引脚映射恰好仍与原分配相同。

自由 I/O 轮最差 hold 仍为 clk9 → clk1：`FDRE_inst_bmovb/C` → `FDRE_inst_bvzmv/D`，数据路径 0.168 ns，时钟偏差 2.690 ns，跨 SLR 补偿 0.356 ns。相较主实验最差路径的 2.538 ns 时钟偏差，本轮最差偏差更大。尽管 hold 违例端点减少，WHS 和 THS 均更差，释放自动 I/O 约束没有在这次对照中解决保持时间问题。

该轮 `summary.json` 的 `implementation_verified=true` 仍仅表示完整布线且 DRC 无 Error；必须同时保留 `critical_warnings=1` 和 `timing_met=false`，不能据此称为全部验收通过。

自由 I/O 最终 DCP（仅服务器保存）：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/clk-fpga08-u250-vivado-free-io-10ns-20260929-200515-458636/reports/vivado_routed.dcp
```

SHA-256：`d3afbd36b0ea020b86ff2787e702cb5c48c8a6672884655c49bf51e0c197ff34`。至此两轮原生 Vivado 实验均已结束，均有完整布线检查点，均未完成全部时序收敛。
