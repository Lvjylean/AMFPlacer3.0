# R10 已启动

用户已批准并于 2026-09-29T13:33:45.312741+08:00 启动完整 profiling 实验：/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260929-133345-334380。本页下方保留启动前审阅内容，实际构建和计时采集以 getrf-r10-experiment.md 及 review-manifest.json 为准。

---

# GETRF / U250：R09 → R10 启动前审阅

状态：**旧匹配器、二维聚拢开启、共享比例 0.4 / SA 有效比例 0.32 已获确认；R10 未启动**。核对日期：2026-09-29。

用户指定以下已完成实验为 R09；R10 是下一轮完整实验的逻辑编号，尚未分配运行目录。历史目录及产物保持原名。

- R09：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/getrf-u250-full-20260928-190947-261685`
- R09 DCP：上述目录下 `reports/getrf_routed.dcp`。
- R09 DCP SHA-256：`5260e6ee54eb773e8d55ea6529d59fdb058fad83d592859582f76be5ad36385b`，本次重新计算确认。

本方案以服务器 `configs/experiments/getrf-u250-tile-columns.json` 为基础。用户已确认 R10 使用旧二分图匹配器，并保留二维聚拢开启；专用 R10 草案改为 `BoundaryAwareClustering=true`，与 R09 一致。基础配置文件本身不改动。

## 实际变化

| 项目 | R09 | R10 当前待审方案 | 影响 |
|---|---|---|---|
| 器件坐标文件 | `data/devices/u250-vivado-2024.2/exportSiteLocation.zip` | `data/devices/u250-tile-columns-v3/exportSiteLocation.zip` | 根据真实 tile 列及子站点重新定义 X |
| X 坐标范围 | 0–316 | 0.25–150.75 | 所有 237,632 个 site 的 X 都改变；不是严格统一乘 0.5 |
| Y 坐标与资源 | Y=0–959 | 完全相同 | site 集合、资源类型、BEL、SLR、禁用标记均不变；逐行去掉 X 后完全相同 |
| 基础延迟模型 | 原中段、远段系数 | U250 新坐标拟合的中段、远段系数 | 近段系数、公式形式、局部 `X *= 2` 保持不变 |
| 物理边界模型 | `u250-physical-v1/model/physical_structure.tsv` | `u250-tile-columns-v3/model/physical_structure.tsv` | HPIO 分界 X=158 → 74.75；区域横向范围随坐标重建 |
| 二维聚拢 | `BoundaryAwareClustering=true` | **`true`，用户已确认** | 保留容量感知 X/Y 区域聚拢及物理边界延迟 |
| 共享纵横比例 | `y2xRatio=0.71` | **`y2xRatio=0.4`** | 恢复原版共享设置，影响 QP、加权 HPWL、合法化、打包及二维聚拢位移 |
| SA 有效纵横比例 | 显式 0.71 | **0.32** | 删除显式 SA 覆盖值，采用原版 0.4×0.8 回退规则 |
| 二分图匹配求解器 | 原旧实现 | 显式 `BipartiteMatchingBackend=legacy` | 保留旧实现自身正向边 +0.01、反向边不对应抵消的行为；不使用新匹配内核 |
| 扩散受限位移 | disY 误用 X 位移 | **disY 使用 y-lastSpreadY** | 恢复二维欧氏步长限制；新增用户授权修复 |
| 宏候选选择 | 原候选成本计算和排序 | `MacroCandidateSelection=cached_topk` | 保留成本缓存、top-K 和只读列表引用优化 |
| 候选成本交叉验证 | 无此新开关 | 显式 `VerifyMacroCandidateCosts=false` | 对应当前普通配置的默认值；不额外重复计算候选成本 |
| AMF 构建 | `build-20260928-002048-349585-d534237b` | `build-20260929-131650-338460-c70df682` | 使用包含新延迟参数、旧匹配默认值及扩散 Y 位移修复的冻结构建 |

旧候选选择与缓存候选选择曾在旧坐标同构建对照中得到相同 HPWL 轨迹和导出结果。该证据不等于已经验证新坐标下的完整布局布线结果。

冻结源码共有 38 个变化或新增文件，完整哈希列表保存在审阅清单中。除匹配后端选择、候选缓存/排序与引用优化、U250 延迟系数外，大量差异来自 profiling 宏、会话管理、构建选项和测试。候选构建 `runtime_profiling=false`。QP 求解器、SA、扩散、打包等算法未因 profiling 插桩切换到新算法，但它们使用的坐标数据会改变。

## 不变的设置

| 项目 | R09 与 R10 |
|---|---|
| 设计输入 | 同一 GETRF post-opt DCP、完整网表、时钟文件和四份资源映射文件；哈希已核对 |
| AMF 目标周期 | `ClockPeriod=10` ns |
| Vivado 时序约束 | 从相同原始 DCP 读取；不添加覆盖约束 |
| 初始全局布局设置 | `GlobalPlacementIteration=30`；SA restart=10、IterNum=100000 |
| SA 分格配置 | `clockRegionXNum=8`、`clockRegionYNum=16` |
| 物理边界延迟 | `PhysicalBoundaryMode=true`；SLR 1.5 ns/道，HPIO 0.5 ns/道 |
| SLR 接缝 | Y=239.5、479.5、719.5 |
| 区域资源容量 | 8 个区域；逐区域的资源容量完全相同 |
| 数值保护 | `TimingMaxEnhancement=1000`、`QPStabilityGuard=true`、`PseudoNetWeight=0.0025` |
| 并行设置 | AMF jobs=8；Vivado maxThreads=4 |
| Vivado 后端 | Vivado 2024.2，相同 `place_design`、`route_design` 与严格导入验收 |
| 后端 Tcl | SHA-256 与 R09 完全相同：`b3a42fa5c7f46774b71fe4676354c053b961650f14e929abd4ba04c09c5eb999` |
| 当前功能范围 | 不引入 SLL 容量/拥塞优化，也不接入外部 partition/floorplan |

用户已放弃 0.71，R10 恢复共享 0.4、SA 有效 0.32，作为新坐标下的几何平衡候选。新坐标每个 SA 格为 18.8125×59.9375，纵向乘 0.32 后为 19.18，与横向 18.8125 接近。该选择不是新的延迟回归结论。扩散的固定 bin 尺寸和其他距离阈值尚未统一重标定。

## 新延迟系数的具体范围

令 `dx=|x1-x2|`、`dy=|y1-y2|`，基础延迟为：

```text
d_ns = [c0 + c1*(2*dx)^0.3 + c2*dy^0.3
             + c3*(2*dx)^0.5 + c4*dy^0.5] / 1000
```

分段用乘 2 之前的 `dx²+dy²`：小于 9 为近段，9 至小于 36 为中段，36 及以上为远段。物理边界模式再使用基础延迟下限与跨界惩罚。`X *= 2` 是旧版已有的特征变换，不是本次新增的全局坐标缩放。

| 分段 | R09 系数 c0…c4（ps） | R10 系数 c0…c4（ps） |
|---|---|---|
| 近段 | 95.05263521, −26.50563359, 77.42394117, 106.29195883, −14.975527 | 相同 |
| 中段 | 123.05017047, −169.25614191, −117.28028144, 208.53573639, 174.2573465 | 160.1553292055571, −64.94911625084538, 22.807442252015385, 120.36632967195294, 27.120924804902547 |
| 远段 | 234.7694101, −433.99467294, −64.96319998, 373.78606257, 139.45226658 | 202.12777470189823, −146.96155453685554, −131.59702340956898, 163.46881911227194, 136.17523346378107 |

候选构建按器件名 U250 选用新系数，因此必须与新坐标文件配套。区间阈值虽然没变，同一连接因为 dx 改变也可能进入不同分段。不能仅按系数变化预测最终 WNS。

## R09 参考结果与 R10 统计口径

| 指标 | R09 |
|---|---:|
| WNS / TNS | +0.020 ns / 0 |
| setup / hold 违例端点 | 0 / 0 |
| AMF 进程墙钟 | 64.89 分钟 |
| Vivado 导入布局 | 10.64 分钟 |
| Vivado place_design | 3.05 分钟 |
| Vivado route_design | 27.71 分钟 |
| 总墙钟 | 119.50 分钟 |

AMF 进程墙钟含内部输入、布局、导出与退出，不能全部记为纯 placement。R10 会继续将 Vivado 导入、place_design、route_design、审计/报告和 DCP 写出分别列出；AMF 内部适配若无独立计时，标记“未单独计时”，不能算成已测得的纯布局时间。此待审方案不额外开启逐函数 profiling。两轮都复用网表导出缓存，首次 DCP→AMF 导出成本不在本次运行时间内。

新旧坐标单位不同，原始 HPWL 数字不能直接计算改善率。端到端主要比较相同约束下的 routed WNS/TNS、合法性验收、实际分段耗时；如需比较几何线长，应把两份布局映射到共同坐标再计算。

## 待确认材料与执行方式

服务器审阅目录：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20260929-getrf-r10-review/`。

- `r10-proposed.json`：从当前新坐标配置复制，按用户确认开启二维聚拢，设置共享比例 0.4，移除显式 SA 比例以回退到有效 0.32；显式写出 legacy、cached_topk、候选成本交叉验证关闭。
- `review-manifest.json`：R09/R10 标签、完整配置、输入/构建哈希、坐标与区域容量核对、源码和脚本差异，状态 `awaiting_user_confirmation`。
- 本文同步于服务器 `docs/research/getrf-r09-r10-review.md`；轻量副本保存在本地项目同名位置。

候选二进制 SHA-256：`7581896c9a32d91433c4ac9bbc1a07c45570dd32d647acfe2282dd573a966619`。

批准后采用完整新布局 → 严格导入 → Vivado place_design → route_design → 审计/报告/DCP 的流程。输入仍为 `/Projects/jinyang/workspace/AMFplacer3.0/data/reference/getrf-u250/post_opt.dcp`，不是 R09 的 routed DCP，不复用 R09 placement。最终 DCP 只保存在服务器新运行目录。

以下命令仅用于审阅，**尚未执行**：

```bash
cd /Projects/jinyang/workspace/AMFplacer3.0
python3 scripts/amf3.py full-run \
  --config experiments/preflight/20260929-getrf-r10-review/r10-proposed.json \
  --binary builds/build-20260929-131650-338460-c70df682/build/AMFPlacer \
  --dcp data/reference/getrf-u250/post_opt.dcp
```

用户已确认二维聚拢开启、旧匹配器、共享比例 0.4 与 SA 有效比例 0.32。专用草案与哈希均已更新；仍未执行启动命令。

## 历史 0.71 的来源与作用补充（R10 已弃用）

legacy 是“旧实现”的配置枚举值，指二分图匹配后端，不指 QP 求解器。它在合法化时为待放置对象选择不冲突的候选位置。

0.71 原先用于将纵向距离折算成横向距离的成本单位：D=|Δx|+0.71|Δy|。它不是 0.71 ns，也不统一缩放物理坐标。旧坐标下拟合 delay≈bx·dx+by·dy+其他项，得到 bx=0.0104251 ns/X单位、by=0.00745117 ns/Y单位；by/bx=0.714734，取 0.71。该标定最初面向 SA，不是完整非线性延迟模型的重新拟合。

- Simulated Annealing y2xRatio=0.71：初始簇 SA 的簇间距离、固定对象连接距离，以及同格惩罚中的 (regionW+0.71·regionH)。早期缺省有效值为共享 0.4×0.8=0.32，后来显式设为 0.71。
- y2xRatio=0.71：共享给加权 HPWL、合法化/打包搜索、二维聚拢位移成本，以及 QP 的 Y 方向边/伪网权重。最初 SA 标定阶段仍为 0.4，R08/R09 才与 SA 一同设为 0.71。
- QP 在组装 Y 方向矩阵时对部分权重乘 0.71，不是将所有 Y 坐标替换成 0.71Y 再平方；有些目标在调用侧还作反向补偿，不能把整个 QP 简化成统一一个纵向系数。
- 基础非线性延迟公式中的 X *= 2、SLR/HPIO 跨界附加延迟属于另外的机制。

R09 两个配置键同为 0.71，但范围不同。新 X 坐标让同一物理横向距离的数值变小，保留 0.71 会提高纵向相对横向的成本。若理想情况下仅有 x_new=s·x_old，为保持加权曼哈顿距离的相对比例，需要 ratio_new=s·ratio_old；例如 s=0.5 对应 0.355。实际 tile 列映射并非统一缩放，该例不能直接作为 U250 的新标定结果，也不保证整套 QP 和其他目标完全等价。


## QP、扩散、合法化的缩放核对

以 R10 冻结构建源代码为准，令 eta=y2xRatio=0.4：

- QP 的 B2B Y 边权重为 eta·w·enhance/max(minDist, |Δy_old|)，对应二次项乘 (y_i−y_j+pinOffset差)²；X 边没有 eta。时序引导 pin-pair 权重和通用 Y 向伪网也乘 eta。位置坐标本身不会因此直接乘 0.4。
- 用户簇及二维物理区域 Y 锚点会在调用前除以 eta（区域锚点使用 max(0.01,eta)），随后底层再乘 eta。在当前 0.4 下两者抵消，区域锚点自身权重不随 eta 一起缩小。X/Y 分开求解，若某个独立目标所有项只统一乘正数，其最优点不变；不能据此把最终纵向移动描述成原来的 40%。
- 合法化使用 |Δx|+eta|Δy| 计算位移，也使用 weighted HPWL=(xmax−xmin)+eta(ymax−ymin) 的变化计算候选成本。具体模式可能再按宏 cell 数归一化。打包和二维聚拢位移也读取共享 eta。
- GeneralSpreader 不直接读取 y2xRatio；完整布局入口的 bin 尺寸依次为 5×5、2.5×2.5、2×2，都是原始坐标单位，并不随 eta 自动变成加权空间中的正方形。
- 智能扩窗主要比较各方向资源利用率/容量，横向优先判断含固定系数 0.9。它是方向选择偏置，不是坐标换算系数；简单扩窗路径采用方向试探。资源容量收缩比例同样不表示横纵缩放。
- 扩散目标回写用同一个 forgetRatio：p_new=alpha·p_spread+(1−alpha)·p_previous，X/Y 使用相同 alpha。这是移动步长/平滑系数，不是纵横比例。
- 共享 eta 会通过 QP、HPWL 和其他阶段间接改变扩散所接收的布局；不能把“扩散没有直接读取 eta”解释成改变 eta 后扩散结果一定不变。

可定位的冻结源码行（均相对上述 source_snapshot）：PlacementInfo.h:2269、2321、3239；WirelengthOptimizer.cc:1075、1119；MacroLegalizer.h:747、784、790；BoundaryAwareClusterer.cc:103；AMFPlacer.h:340、367、398；GeneralSpreader.h:490；PlacementInfo.h:1071。

原上游继承缺陷已按用户要求修复：PlacementInfo.h:1099 的 disY 改用 abs(y-lastSpreadY)×forgetRatio。R09 和旧候选构建保留原样；上述新 R10 候选已包含修复。GlobalPlacer 在 timingOptimizer.getEffectFactor()>=1 时启用 displacementLimit=10，启用条件、阈值和平滑逻辑未变。该修复不引入 y2xRatio 加权。10 个生产代码回归用例在旧代码失败 5 个，新代码全部通过。


## 共享比例恢复与延迟拟合核对（2026-09-29）

共享 y2xRatio 从 0.71 恢复为 0.4 不改变延迟回归特征：分段仍以原始 dx²+dy² 判断，特征仍为 1、(2dx)^0.3、dy^0.3、(2dx)^0.5、dy^0.5。基础延迟、下限、SLR/HPIO 惩罚不读取该共享比例。对 8,582 组相同坐标分别设置 0.71 和 0.4，生产接口的完整输出（含 4 个 STA 探针）逐字节一致。因此仅恢复比例不需要重新拟合，也不应撤销已针对 tile-columns-v3 坐标拟合的 U250 中段/远段系数。

比例和本次限幅修复会改变最终布局，从而间接改变连接距离与布线分布；应在后续完整实验中检验预测误差与时序，而不是预先假定必须重拟合。更换坐标映射、改变 X*=2/分段特征或发现明显系统性误差时，再重新评估拟合。

证据：experiments/evidence/20260929-spread-displacement-fix/manifest.json。新构建通过完整编译与专项检查；R10 仍未启动。
