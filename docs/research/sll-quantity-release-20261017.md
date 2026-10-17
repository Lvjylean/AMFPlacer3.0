# SLL 数量建模与 QP 优化发布（2026-10-17）

分支：`sll优化`。在已发布的普通 Pro / SRL 兼容两轮基线上整理现有 SLL 实现及证据。默认配置仍为普通 Pro，显式 `ProPrimitiveTiming=false`，SLL 数量目标默认关闭。

## 来源与工作区

| 内容 | 来源与处理 |
| --- | --- |
| GitHub 基础 | `c17c6247b22f294cdd75a925dd171b9192e8c46f`，`amf3pro_SRL兼容` |
| SLL 算法实现 | eda099 独立仓库提交 `c543fe540cdf145f63d2c896ab0afa44763fb6fc` |
| GP 验证与脚本 | 同仓库 `0cc1e50`，完整提交及文件哈希见发布清单 |
| 后端脚本 | 原工作区未提交的 `launch_backend_comparison.py`，本次纳入 Git 并记录哈希 |
| 本次整理位置 | eda072：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261017-sll-github-release/repo` |
| 原开发位置 | eda099：`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01/repo`，原目录保留 |

两端已有开发工作区、历史配置、实验和默认构建均未改写。本分支不是两台服务器全部临时文件的快照；只纳入 SLL 相关代码、配置、脚本、文档及轻量证据。DCP、输入网表、构建目录、运行日志和缓存不新增到 Git。

与发布基础相比，9 个 SLL 源文件直接取自验证快照，保持逐字节一致；CMake 只新增独立检查目标。保留已发布的 `check_hierarchical_boundary_policy.cc` 测试接口修正，其余既有源码不变。快照提交属于独立仓库，不假定它们在公共 Git 历史中；[发布清单](../provenance/sll-quantity-release-20261017.json) 保存来源和 SHA-256。

旧 `variants/merged`、旧 SRL 核验脚本及 10 月 12 日说明用于历史复现。SLL 本次只接入根目录独立两轮版本；不要在本分支覆盖合并版文件运行 SLL。重现旧合并版应检出父提交 `c17c6247` 的独立副本。

## 已实现范围

| 代码 | 职责 |
| --- | --- |
| `src/lib/HiFPlacer/placement/globalPlacement/SllQuantityModel.h` | 按 net 计算离散跨界需求、SoftFloor + WASLL 平滑代价、解析梯度、按 net 并行计算与线程结果汇总 |
| `SllQuantityOptimizer.{h,cc}`（同目录） | 从真实器件边界及 PU/pin 映射构建模型；冻结原 QP，加入局部目标，检查候选并决定接受；输出诊断 |
| `WirelengthOptimizer.{h,cc}`（同目录） | 每次 QP 的模型接入与坐标提交 |
| `GlobalPlacer.cc`（同目录） | 各迭代阶段的诊断挂接 |
| `placementInfo/PlacementInfo.cc`、`SllClipTrace.h` | 记录区域裁侧造成的坐标投影 |
| `src/tests/check_sll_quantity.cc` | 整数计数、有限差分核验、并行一致性、合成跨 SLR 移动等 86 项检查 |

当前支持已验证的纵向 `1×4` SLR 拓扑，边界读取物理元数据；U250 为 239.5、479.5、719.5。pin 是网表引脚，坐标包括 PU 锚点、宏内 cell 偏移和 pin 偏移。离散量为每个 net 的最高与最低 SLR 编号之差，同侧多个 sink 不按 driver–sink 对重复计数。

S 统计观察到的普通网络，优化集合另要求至少两个 pin、恰好一个 driver，并应用可选扇出过滤。电源/常量和 AMF 标记的全局网络排除。`S_optimized` 与平滑目标使用相同的优化集合；无外部 driver 坐标时不补造端点。

每轮在当前坐标计算非线性梯度，然后冻结该梯度，为当轮 Y 方向 QP 加入线性项和正对角稳定项。对于 `Q(y)=1/2 yᵀAy+bᵀy`，改为 `A += λρI`、`b += λ(g−ρy₀)`，仅作用于可移动对象。候选检查使用冻结的原 X/Y QP 加实际 WASLL，结合位移限制、合法位置与回溯；接受前不写回 PU 坐标。后续扩散、裁侧或合法化仍可能增大整数 S，不能承诺每轮完整流程都单调下降。

实际布局使用解析求导，有限差分仅用于小算例的梯度验证。实现包含按 net 的 OpenMP 并行计算；不是本轮新做的整个 placer 多核加速。空间需求、热点优化、虚拟 SLL cell 和对 Vivado 的 SLL 位置约束均尚未实现。

## 配置

所有值沿用 AMF 的字符串配置形式。

| 键 | 默认值 | 意义 |
| --- | --- | --- |
| `SllQuantityEnable` | `false` | 是否把数量目标接入 QP；开启后仍需正权重 |
| `SllQuantityDiagnostics` | `false` | 仅开启统计与追踪，不等于开启数量优化 |
| `SllQuantityWeight` | `0` | λ；已有 D 实验为 100 |
| `SllQuantityTau` | `30` | 接缝 sigmoid 过渡宽度，单位为布局 Y 坐标；当前不做退火 |
| `SllQuantityGamma` | `0.25` | WASLL 的软编号温度，与 tau 不同 |
| `SllQuantityCurvature` | `0.002` | 局部正对角稳定系数 ρ，不是精确 Hessian |
| `SllQuantityMaxStep` | `10` | 单次候选 Y 位移上限 |
| `SllQuantityMaxFanout` | `0` | 0 表示不限制优化网络扇出 |
| `SllRegionPolicy` | `preserve` | `preserve` 或 `release-single` |
| `SllReportDirectory` | 无 | 请求 SLL 功能时必须提供；每轮使用新的绝对路径 |

`release-single` 只释放可移动、未锁定、未打包的普通 LUT/FF 的启发式区域偏好，不释放宏或 LUTRAM；选中项的 X/HPIO 与 Y/SLR 偏好一起释放。它不是均衡局部 SLL 密度的热点策略，也不解除固定资源的硬约束。

| 比较组 | 区域策略 | 数量目标 | 诊断 |
| --- | --- | --- | --- |
| A / 新二进制关闭对照 | preserve | 关 | 可开；GP 已验证与旧二进制坐标完全一致 |
| C | release-single | 关 | 开 |
| D | release-single | 开，λ=100 | 开 |
| 可配置的其他组合 | preserve | 开 | 开；本次 A/C/D 证据不包含此组合的完整结果 |

报告为 `parameters.tsv`（有效参数）、`mapping.tsv`（映射和网络过滤）、`stages.tsv`（各阶段 S/平滑目标/跨界移动/裁侧计数）和 `qp.tsv`（候选目标变化、接受率、步长及回溯）。报告路径不会由 full-run 自动改名，重用同一路径会覆盖这些文件。

## 新检出仓库如何运行

从本分支根目录执行，按 [输入准备说明](amf3pro-srl-compatible-20261012.md#配置输入和构建) 准备服务器上的 GETRF 输入和 DCP。数据相对路径及已验证哈希沿用原发布清单。本分支源码核验应使用新的入口：

```bash
export AMF_MACHINE_CONFIG=configs/machines/eda072.json
# eda099 改为 configs/machines/eda099.json
python3 scripts/verify_sll_release.py
python3 scripts/verify_sll_release.py --inputs
python3 scripts/amf3.py build --profile --jobs 8
```

需要 C++17、CMake、Ninja、OpenMP、Boost 开发库。若使用已有自建 Boost，按所在主机设置 `AMF_STUDY_BOOST_LIBRARYDIR`，不要复制另一台主机的依赖路径。

下面从便携普通 Pro 配置生成新的 D 配置。改 `SLL_CASE` 为 A/C 可生成对应对照，其他阶段预算和参数不变；这里 A 使用新二进制关闭目标并开启诊断，历史后端 A 使用旧普通 Pro 二进制。

```bash
export SLL_CASE=D
SLL_CASE_DIR=$(python3 - <<'PY'
import datetime, json, os
from pathlib import Path
case = os.environ['SLL_CASE']
assert case in ('A', 'C', 'D')
root = Path.cwd()
out = root / 'experiments/preflight' / ('getrf-sll-' + case.lower() + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
out.mkdir(parents=True, exist_ok=False)
config = json.loads((root / 'configs/experiments/getrf-amf3pro-srl-share4-10ns.json').read_text())
assert config['ProPrimitiveTiming'] == 'false'
config.update(SllQuantityDiagnostics='true', SllQuantityEnable='true' if case == 'D' else 'false',
              SllQuantityWeight='100' if case == 'D' else '0', SllQuantityTau='30',
              SllQuantityGamma='0.25', SllQuantityCurvature='0.002', SllQuantityMaxStep='10',
              SllQuantityMaxFanout='0', SllRegionPolicy='preserve' if case == 'A' else 'release-single',
              SllReportDirectory=str(out / 'sll'))
(out / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
print(out)
PY
)
export AMF_MACRO_STUDY_MODE=control
export AMF_MACRO_STUDY_START=1
export AMF_MACRO_STUDY_OUTPUT="$SLL_CASE_DIR/diagnostics"
export AMF_LEGALIZATION_TRACE=1
export AMF_MATCHER_WORKER_TRACE=1
mkdir "$AMF_MACRO_STUDY_OUTPUT"
python3 scripts/amf3.py full-run --profile --global-only \
  --config "$SLL_CASE_DIR/config.json" --run-prefix getrf-sll
```

完整后端运行需**重新生成一个新目录和配置**，再去掉 `--global-only`；保留实际 Vivado placement 执行。`configs/experiments/sll-*-final.json` 和 `sll-quantity-baseline.json` 是当时冻结的绝对路径配置，不作为新实验入口；[历史脚本说明](../../scripts/diagnostics/sll_quantity/README.md) 区分运行脚本和一次性源码补丁脚本。

## 已完成实验及结论

GP 详细验收见 [2026-10-13 报告](sll-quantity-qp-validation-20261013.md)。后端汇总见 [原始 JSON](../provenance/sll-quantity-20261013/backend-results.json)。下表保留 AMF 进程墙钟与 Vivado placement 各自的口径。

| 指标 | A：普通 Pro | C：仅释放偏好 | D：释放偏好＋数量目标 |
| --- | ---: | ---: | ---: |
| GP 结束离散需求 S | 11,085 | 14,372 | 12,396 |
| GP 实际轮数 | 37 | 37 | 40 |
| Vivado Total SLLs Used | 11,656 | 14,926 | 13,036 |
| WNS / ns | 0.160 | 0.022 | 0.122 |
| TNS / ns | 0.000 | 0.000 | 0.000 |
| AMF 进程墙钟 / min | 18.34 | 19.06 | 21.07 |
| Vivado placement / min | 4.43 | 4.31 | 4.03 |
| Vivado routing / min | 23.69 | 20.18 | 22.96 |
| 实际总墙钟 / min（包含 placement） | 68.86 | 64.86 | 68.01 |

D 相对 C 的最终 GP 需求减少 13.75%，后端 SLL 使用量减少 12.66%；但 D 两项均高于 A，不能宣称数量优化优于普通 Pro。三组均完成全量布线，setup/hold 无违例，DRC Error/Critical Warning 为 0，原始 LOC/BEL 保留率为 100%。S 是布局拓扑估计，Vivado 的使用量是另一个指标，二者不能互换。

相同配置阶段预算和退出条件下，D 实际多运行 3 轮。每组后端仅一次共享服务器上的串行运行，不能从单次墙钟推出普遍加速。实际总墙钟包含导入、兼容处理、审计、报告、DCP 写出等，不能用三个阶段简单相加替代。今后 DFX 指标如排除 placement，必须标注“扣除 Vivado placement”，同时保留实际总墙钟和单列兼容处理时间。

此前 GP 验证的 86 项检查通过，有限差分最大误差约 `1.15e-11`，串行/8 线程误差约 `5.77e-15`；关闭功能的 856,998 个 cell 坐标和 PU 编号与普通 Pro 一致。真实数量实验的 80 个 QP 候选全部直接接受，因此尚未用真实设计覆盖拒绝和回溯分支。本次发布再次编译和检查的结果单独记录于 `docs/provenance/sll-publication-checks-20261017.json`；未为整理发布重新运行完整 GETRF/Vivado。
