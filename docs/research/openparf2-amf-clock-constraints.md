# OpenPARF 2.0 与 AMF 的时钟约束：源码和实验核查

核查日期：2026-10-03。范围：服务器当前 AMF3、实际用于原版对照实验的冻结 AMF2、官方 OpenPARF 2.0.0，以及已有 CLK-FPGA08/MiniMap2 日志。未修改布局器源码，未重跑布局布线，未执行 OpenPARF 与 AMF 的同输入对照。

## 结论与论文措辞

“AMF 没有实现 clock constraints”不能直接用于 AMFPlacer 2.0 或当前 AMF3。两者已有时钟网络识别、区域使用量统计、软疏散和 CLB 打包时的半列容量硬检查；缺口是区域级时钟规划及合法化不完整，最终区域超限也未成为 AMF 输出的强制失败条件。

OpenPARF 2.0 的区别主要是：显式进行时钟区域分配，生成允许区域矩阵，修复半列超限，并把这些约束传入后续合法化和详细布局。它同样使用抽象时钟模型，不能据此声称已实现全部 Vivado 时钟路由规则。

本地《LEAPS: Topological-Layout-Adaptable Multi-Die FPGA Placement for Super Long Line Minimization》PDF 第 7 页 Table II 将 AMF-Placer 的 Clock Constraints 标为 ×，第 12 页实验设置再次据此排除 AMF；但第 14 页参考文献 [23] 指向 ICCAD 2021 的 AMF-Placer，**不是 AMF2 论文或本项目 AMF3**。论文没有给出足以把该 × 对应到上述每个源码能力的细分判据。因此不能把比较表翻译成“所有版本 AMF 均无任何时钟约束代码”。

AMF 作者的公开[时序、时钟与拥塞说明](https://zslwyuan.github.io/AMF-Placer/_5_5_timing_clocking_congestion.html)本身也介绍了 clock-region-aware 优化和 half-column 时钟拥塞处理。

## clock constraints 在这里指什么

| 层次 | 约束内容 | 与本次比较的关系 |
| --- | --- | --- |
| 时序目标 | 时钟周期、setup/hold、WNS/WHS | 8 ns/10 ns 属于此类。AMF 有 ClockPeriod 参数；不能用周期参数存在证明物理时钟资源合法。 |
| 时钟网络物理容量 | 一个 clock region、half-column 可承载的不同网络数，以及负载的允许区域 | OpenPARF CNP 和本次 AMF 容量核查的主要对象。 |
| 更完整的实现约束 | 时钟源位置、专用轨道/连接、同步器分组等器件和设计语义 | 仍需后端检查；不能把 ASYNC_REG 导入冲突都归为 24/12 容量错误。 |

[ISPD 2017 官方规则](https://www.ispd.cc/contests/17/legalization.html)规定每个 clock region 至多 24 条时钟网络、每半列至多 12 条；一条时钟负载所跨区域的矩形覆盖也需计入中间区域。这是该 benchmark 的容量模型；不把 24/12 无条件推广到所有器件、所有资源类型。

容量数的是**不同网络**，不是寄存器个数或 fanout。例如 1 万个 FF 共用 3 条时钟，网络数是 3；25 个 FF 分别用 25 条时钟，网络数可达 25。统一频率不会合并物理上独立的网络。非 STA 时钟的 BUFG 复位/使能网络仍可能占用全局布线资源。

## 核查版本和证据保存

服务器项目根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

| 对象 | 实际位置和版本 |
| --- | --- |
| 当前 AMF3 | 项目工作树；HEAD `c70df682d8b4bdbe9b71b56008d1087d52ddad51`，含未提交工作，不能只用 HEAD 代替完整源码快照。 |
| 原版 AMF2 实验构建 | `builds/upstream-amf2-20260930-101827-215260/src`；提交 `70d98288153046ea4fd07190e748b6530e3042f5`；构建 manifest 的 `source_modified=false`。原版 MiniMap2/VU095 实验使用该冻结二进制。 |
| OpenPARF2 | `data/reference/openparf-2.0`；官方 tag `2.0.0`；提交 `eb1b6bce9be9da992b285a85d572a7f9c76fe92a`；工作树干净。 |

[源码审计摘录及逐文件 SHA-256](clock-constraints-audit/sources.json)保存本次关键片段及真实行号。以下 AMF 路径相对服务器根目录；OpenPARF 路径相对其仓库根目录。旧 `/Projects/jinyang/AMF-Placer` 的当前 HEAD 不能代替原版实验的冻结源码身份。

## OpenPARF 2.0 的具体实现

| 步骤 | 源码位置 | 行为 |
| --- | --- | --- |
| 启用时钟约束流程 | `openparf/placement/op_collections.py:551–560` | `confine_clock_region_flag` 选择 ClockAwareDirectLegalize。13 个 ISPD2017 回归配置均启用相关开关。 |
| 时钟区域规划 | `openparf/ops/clock_network_planner/clock_network_planner.py:36–63` | 尽量减少移动，输出 instance→CR 分配和 clock→允许 CR 矩阵；该算子在 CPU 上运行。 |
| 处理 CR 拥挤 | `openparf/ops/clock_network_planner/src/utplacefx/ClockNetworkPlanner.cpp:648–677,1915–1927` | 使用 BBox 覆盖模型；超容量则规划失败，生成新的区域禁用 mask 并尝试重新分配。 |
| 后续布局使用规划结果 | `openparf/placement/placer.py:842–884` | 允许 CR 矩阵进入 fence/energy-well、硬资源合法化、直接合法化及 ISM。 |
| 半列容量修复 | `openparf/ops/direct_lg/src/dl_solver.cpp:305–329,361–395,429–437` | 计入固定 DSP/RAM 的时钟；超限时禁用移动代价较低的可移动时钟，再以 rip-up 合法化移动相关负载。 |
| 详细布局维持限制 | `openparf/placement/placer.py:1903–1912` | ISM 使用 CR 和半列的允许矩阵。 |
| 合法性检查 | `openparf/ops/legality_check/src/legality_check.hpp:189–260` | CR 按时钟负载的区域包围盒计数，半列按实际负载时钟集合计数，超限返回 false。 |

默认容量见 `openparf/params.json:349–364`。不能仅因其中 `honor_*` 默认 false 就判断实际流程关闭：clock-aware C++ 入口 `openparf/ops/direct_lg/src/direct_lg.cpp:95–96` 会启用两个约束，ISPD2017 配置也明确开启。普通入口 44–45 则关闭。

两个需要避免夸大的边界：

1. `clock_network_planner.py:17` 明确说明目前只实现 clock-region assignment；C++ 中 D/R-layer 真正路由调用在 634–646 行被注释，实际运行 BBox 模型。因此不是完整器件时钟路由器。
2. **OpenPARF 的 Python 最终检查也不是强制失败门槛。** `placer.py:1855–1864,1898–1902,1950–1955` 收到 `legal=false` 后只记录 `Placement is not LEGAL` 警告。不能把“检查不通过必然退出”作为 OpenPARF 相对 AMF 的优势；更明确的差别是前面的规划、区域限制与修复算法。

## AMF2 / 当前 AMF3 已经实现哪些约束

| 能力 | 当前源码位置 | 核查结果 |
| --- | --- | --- |
| 目标时钟周期 | `src/lib/HiFPlacer/placement/placementTiming/PlacementTimingInfo.cc:32–60` | 读取 `ClockPeriod` 及按 driver 指定的周期；与物理容量是不同层次。 |
| 时钟集合输入 | `src/lib/HiFPlacer/designInfo/DesignInfo.cc:375–420` | 从 clock file 标记网络及相连 cells。输入中未列出的全局网络不会自动全部纳入该集合。 |
| CR 及粗略半列检查 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc:2105–2179` | 统计时钟 pin bbox 覆盖区域；检查 CR≤24、粗略半列≤12，并返回布尔值。 |
| 半列硬检查 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h:4263–4274` | 将当前半列网络与候选 PU 网络取并集，仅当数量≤12时允许加入。 |
| 打包真实调用硬检查 | `src/lib/HiFPlacer/placement/packing/ParallelCLBPacker_PackingCLBCluster.cc:761–762` | 超限直接拒绝候选，不是只有日志。`ParallelCLBPacker_PackingCLBSite.cc:229–238`再次筛选并登记占用。 |
| 登记占用断言 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h:4322–4329` | 登记后断言不超过半列容量。 |
| 时钟拥挤软疏散 | `src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.cc:1436–1468` | `CR count > 12 && half-column count > 10` 时膨胀 FF 面积，让密度优化推动负载疏散。 |
| 最终检查结果处理 | `src/app/AMFPlacer/AMFPlacer.h:434,451–453` | 调用 `checkClockUtilization(true)`，不处理返回值，随后输出 `Placement Done`。 |

原版 AMF2 冻结源码也具有上述半列硬检查，见其 `PlacementInfo.h:4242–4253`；打包拒绝调用在 `ParallelCLBPacker_PackingCLBCluster.cc:764–765`。因此不能说这些约束仅由 AMF3 新增。两版本关键容量比较均为 `<=12`，正好 12 条合法。

AMF 还存在 clock-region-aware 吸引与区域列偏好，但这不等同于 OpenPARF 的联合 clock→CR 可用性规划。`clockRegionAware=false` 也不等于关闭全部容量检查；半列集合检查并未受该开关控制。

模型覆盖边界也有源码证据：`src/lib/HiFPlacer/deviceInfo/DeviceInfo.cc:437–442` 为 SLICE 建半列映射，跳过 BRAM/DSP；`DeviceInfo.h:802–806` 明言全局阶段用均匀列宽/列高近似统计，不完全符合真实列分布。因此需要区分“全局阶段的粗估”“CLB 打包阶段的半列约束”和“Vivado 真实轨道资源检查”。

## 既有实验是否支持：CLK-FPGA08 / U250

| 实验 | 结果及证据 |
| --- | --- |
| AMF 布局 `clk-fpga08-u250-r10-10ns-compat-full-20260929-185326-239497` | `logs/amf.log:5645–5661` 最终 CR 矩阵中 X4Y6=32；5672 行对应粗略半列峰值=9；5679 行仍输出 `Placement Done`。 |
| 正确 30 时钟的 AMF 后端重试 `clk-fpga08-u250-r10-10ns-30clocks-full-20260929-192441-680361` | Fabric LOC/BEL 严格导入 469,497/469,497；允许 Vivado 重选 BUFG 位置；`logs/vivado.log:312` 报 X4Y6 计数25超限，399行完整矩阵计数32，481行 `Clock routing failed`。失败在 `place_design`，尚未进入数据网络 `route_design`。 |
| 原生 Vivado，保留相同 I/O `clk-fpga08-u250-vivado-10ns-20260929-194539-494632` | 同输入 DCP、同30×10ns时钟、同332个I/O固定位置；398,083/398,083网络布通，DRC Error/Critical Warning=0/0。 |

配对输入为 `experiments/preflight/20260929-clk-fpga08-timing-correction-192214/corrected/amf_input_30clocks.dcp`，SHA-256 `0a39ec53f2620097de6e58b799eb2c32b1afb1671f43821438339ad117693fa2`。原生 Vivado 自行完成 fabric 与 BUFG 的联合放置，AMF 后端保持 AMF fabric 位置；这是两实验的重要区别。

“25”是初次超限报错时的计数，不能误认为区域最终完整需求只有25。最终 AMF 后端只定义30条真实 STA 时钟；另两条 BUFG 网络传递复位和使能，仍占全局资源。删除错误的两条 `create_clock` 没有让这两条物理网络消失，也没有消除容量失败。

这个 case 对源码机制的支撑尤其明确：

```text
X4Y6 的区域覆盖计数 C = 32 > 24
X4Y6 的粗略半列峰值 H = 9 <= 12
软疏散触发条件：C > 12 && H > 10
此处 32 > 12 为真，9 > 10 为假，整个条件为假。
```

这展示了该联合条件的一种覆盖缺口：一个区域可以因许多时钟分散在不同半列而超限，每个半列却都不拥挤。该数据是最终状态，不能推断它在所有中间迭代均不触发；也不能断言这一个条件是失败的唯一原因。能确定的是：现有处理未能把该输出修复为区域合法，最终检查也未阻止它被标为完成。

原生 Vivado 的 `reports/clock_utilization.rpt:567` 显示 X4Y6 的 HROUTES/HDISTRS/VROUTES/VDISTRS 实际占用为4/8/2/4，各类容量24。它是实际轨道占用，与 AMF BBox 覆盖计数32不是同一指标，不能直接相减。

原生结果 WNS=+0.832ns、WHS=−2.834ns。因此可以说“完成布线且 DRC 无 Error/Critical Warning”，不能说“全部时序通过”。负 WHS 和24/12资源合法性是不同问题；改变10ns周期也不会直接减少独立网络数量。

本实验支持的是：**AMF 当前输出不保证全局时钟区域容量合法，而该设计可由原生 Vivado 在 U250 上完成布线。** 它不支持“该设计无法在U250实现”，也不能在未运行的情况下证明 OpenPARF 必然成功。

关键日志本地镜像：

- [AMF 完成时的资源计数](../../local-reports/clk-fpga08-u250-r10-10ns-compat-full-20260929-185326-239497/logs/amf.log)
- [30条真实时钟的 Vivado 失败日志](../../local-reports/clk-fpga08-u250-r10-10ns-30clocks-full-20260929-192441-680361/logs/vivado.log)
- [原生 Vivado 汇总](../../local-reports/clk-fpga08-u250-vivado-10ns-20260929-194539-494632/reports/summary.json)
- [原生时钟资源报告](../../local-reports/clk-fpga08-u250-vivado-10ns-20260929-194539-494632/reports/clock_utilization.rpt)

服务器对应完整日志位于 `experiments/runs/<run-id>/`。本轮核对的关键日志/报告与服务器 SHA-256 一致。

## MiniMap2 能支持什么，不能支持什么

`amf3-minimap2-u250-full-r10-full-20260930-205123-911245/logs/vivado.log:903–905`记录：请求716,963个位置，实际放置716,764，精确匹配716,267，strict导入失败。诊断发现545个ASYNC_REG、398条同步链边，其中280条跨slice；696个位置不匹配中352个涉及ASYNC_REG，679个位于PCIe区域。

这支持“同步器分组/放置语义未完整传入 AMF，Vivado 导入时需调整”的结论。它与 CLK-FPGA08 的区域容量错误不是同一类证据，也不能证明 OpenPARF 已覆盖 ASYNC_REG 语义。

按用户授权沿用原版 repair 策略后，`amf3-minimap2-u250-full-r10-import-repair-full-20260930-211203-742548`全布通、DRC0，但 `strict_import_verified=false`。这证明 AMF+Vivado 修复流程的该次实现成功，不等于 AMF 独立满足全部放置约束。

## 对 AMF3 后续实现的具体意义

若借鉴 OpenPARF，优先补齐：覆盖真实全局资源网络的输入集合、CR级容量感知的分配/搬移、与硬资源和半列约束一致的合法化，以及后续移动时维持这些约束。区域超限处理不能仅依赖“半列也拥挤”这一联合条件。

实验入口还应把最终资源检查与完成状态关联：估计超限时明确标为未通过并进入修复或失败分支；保留 Vivado 最终时钟放置/路由验收，并分别记录 AMF 原始布局和 Vivado 修复结果。此处是改进建议，本轮未实施代码修改。

OpenPARF 参考源码：[固定版本仓库](https://github.com/PKU-IDEA/OpenPARF/tree/eb1b6bce9be9da992b285a85d572a7f9c76fe92a)。其当前实现也存在最终检查仅警告的边界，借鉴时不应照搬该验收行为。
