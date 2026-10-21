# Historical 2026-10-20 configuration registry

The following describes the frozen 78/83/099 binaries, not the combined branch.

# 常用 placement 配置

日常只修改同目录的 `placer.json`，共 **16 个参数**。它是精简配置入口，输入路径、器件模型、匹配算法和其他高级参数继承所选完整 AMF 基础配置。可视化总开关与三个 Pro 开关在本文件中直接设置。
这里的初值沿用最近 **84 组**：U250、10 ns、jobs=12、cr-slr-hierarchical、独立最多两轮、裁侧开、弱牵引开、区域入口提前停止关、普通全量。
这不是修改了 AMF 的全局缺省值；源码缺省策略仍为 slr-hierarchical。

所有值沿用 AMF 字符串形式，例如 `"true"`、`"10"`。JSON 不支持注释，因此可选值和条件列在此处。

| 实际键名 | 可选值 | 生效关系 |
|---|---|---|
| `device` | `U250`、`VCU108`/`VU095` | 必须与基础配置的器件输入匹配。VHK158 未实现，不可用于运行 |
| `ClockPeriod` | 正数，ns | AMF 目标周期；不自动修改 Vivado XDC |
| `jobs` | 正整数 | AMF 线程数；匹配算法沿用基础配置 |
| `visualization` | `true` / `false` | 坐标与阶段轨迹导出总开关；开启需要 099 可视化副本，默认 false |
| `ProFFClustering` | `true` / `false` | FF 聚类及其相关牵引；独立于 BoundaryAwareClustering，当前 true |
| `ProPrimitiveTiming` | `true` / `false` | Pro 原语时序特例；普通 Pro 基线保持 false |
| `ProLegacyIOBoundary` | `true` / `false` | 旧 VU095 IO 边界特例；U250 必须 false |
| `BoundaryAwareClustering` | `true` / `false` | 新旧聚类总选择器 |
| `noBlockAware` | `false` / `true` | 只在旧路径生效；false 开启，true 关闭 |
| `BoundaryClusteringStrategy` | `slr-hierarchical` / `cr-slr-hierarchical` | 新版聚类开启时生效 |
| `BoundaryStageMode` | `independent-max2` / `merged` | 两种 hierarchical 策略共用 |
| `BoundaryRegionClipping` | `true` / `false` | 新版 SLR/HPIO 偏好区域裁侧；不解除 Pblock 硬约束 |
| `BoundaryWeakAttraction` | `true` / `false` | 新版弱牵引；旧 VU095 风格回退路径也使用，U250 旧列中心路径不使用 |
| `BoundaryAnchorStopOnRegionEntry` | `true` / `false` | 新版进入目标区域是否提前停止牵引；CR 锚点实验当前为 false |
| `ClockRegionColumnStretch` | `true` / `false` | 仅旧聚类启用时生效；实际可配置开关需要 83 版 |
| `IncrementalCompilation` | `true` / `false` | 是否启用 DFX 上下文；具体 full/incremental 模式及缓存来自匹配的基础配置 |

## 一键控制关系

```text
BoundaryAwareClustering = true
  自动保证 PhysicalBoundaryMode=true；基础配置必须已有物理模型
  使用 Strategy / StageMode / Clipping / StopOnRegionEntry
  noBlockAware 与旧纵向拉伸不生效

BoundaryAwareClustering = false
  新版 Strategy / StageMode / Clipping / StopOnRegionEntry 不生效
  noBlockAware = false → 旧聚类开启，可选择旧纵向拉伸
  noBlockAware = true  → 旧聚类关闭
```

精简 JSON 中不生效的值继续保留，方便切回；生成实际配置时剔除，并在 `resolution.json` 中写明原因。
`BoundaryWeakAttraction` 不是关闭全部牵引的开关。

78/82/84 源码本身跳过旧 Y 拉伸，不解析 `ClockRegionColumnStretch`。选择 profile 78 且设置 false 时，生成器不注入这个不支持的键；要在旧路径真正开启拉伸，选择实际支持它的 83 二进制及 profile 83。切换 profile 不会自动替换或构建二进制。

`IncrementalCompilation=true` 不能独立生成静态缓存/Pblock/模块输入。必须配合已有匹配的 DFX 基础配置；反过来，使用含 native 缓存的配置时不能仅把此开关改成 false，生成器会拒绝这种不一致。

## Pro 与可视化

三个 `Pro...` 参数是独立选项，不存在一个可替代它们的通用 `Pro=true/false` 配置键。
关闭 `BoundaryAwareClustering` 不会自动关闭 `ProFFClustering`。`ProPrimitiveTiming=false` 保留普通 Pro 的其他功能，并不表示关闭 Pro。
`ProLegacyIOBoundary=true` 仅允许旧 VCU108/VU095 坐标模型；不得同时加载物理边界模型（包括开启 PhysicalBoundaryAudit 的情况）。

`visualization=true` 使用 `--profile 099-visualization`，并在实际运行时选择 099 的可视化二进制：

`/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261020-visualization-config/repo/builds/config-visualization-recorded-20261020/build/AMFPlacer`

生成器保留此总开关，移除冲突的旧 trace 子键；AMF 运行时自动写入新 `dumpDirectory/visualization` 目录。
`visualization=false` 清除细粒度及七个旧坐标 trace 配置，不影响 `DumpCLBPacking` 等正式布局结果输出。
78/83 不识别该总开关：关闭时由生成器清理旧 trace 字段且不注入不支持的键；开启时明确报错，不会假装支持。
profile 099-visualization 尚未合并 83 的器件策略，不能在这个版本中开启旧 Y 拉伸。
可视化开关只生成数据，不启动查看器、后处理或 GIF 导出。

## 检查及生成

独立工具位于：

`AMFplacer3.0/experiments/preflight/20261020-placement-config/repo/scripts/export_placement_config.py`

在本项目 `AMFplacer3.0` 目录下，检查最近 84 组基础配置：

```bash
python3 experiments/preflight/20261020-placement-config/repo/scripts/export_placement_config.py \
  --config configs/placement/placer.json \
  --base experiments/preflight/20261014-getrf-static-containment/repo/experiments/preflight/20261020-getrf-cr-slr-components-control-84/getrf-cr-slr-10ns.json \
  --profile 78
```

加上一个**尚不存在**的输出目录，就会生成完整的 `amf_config.json` 与 `resolution.json`：

```text
--output experiments/preflight/my-placement-config
```

将生成的 `amf_config.json` 交给原有布局入口。精简的 `placer.json` 本身不含网表等必需输入，不能直接交给 AMF 二进制。
原基础配置不改写；dump 和相关生成文件改到新输出目录。基础配置中的服务器输入路径原样保留，因此生成成功不等于已验证另一服务器的路径、库、缓存或二进制兼容。
工具只检查、生成配置，不运行综合、布局或布线，不自动更新原有实验。

## 统一实验入口（2026-10-20）

两台当前代码副本均使用 `configs/placement/placer.json` 和
`scripts/export_placement_config.py`。新实验通过 `scripts/launch_placement.py`
先读取这份精简配置，再生成本轮完整配置；不要直接编辑历史实验的 `config.json`。
本地项目也提供相同入口。修改文件本身不会启动计算。

在本地 `AMFplacer3.0` 目录下：

```bash
# 读取本地 placer.json，检查配置、目标服务器、二进制能力及输入哈希；不启动
python3 scripts/launch_placement.py

# 以 072 代码副本中刚修改的 placer.json 为本轮来源，同样只检查
python3 scripts/launch_placement.py --config-host 072

# 确定要进行一轮新实验时，显式提交；名称每轮不同
python3 scripts/launch_placement.py --config-host 072 --name my-getrf-run --start
```

调度规则：

| 配置/调用 | 目标 |
|---|---|
| `visualization=true` | 始终 099；即使指定 `--host 072`，也选择 099 |
| `visualization=false`，本地调用 | 默认 072，也可用 `--host 099` |
| `visualization=false`，服务器内调用 | 默认当前服务器 |

072 目前没有直接 SSH 登录 099 的认证。跨机调度由本地已有 SSH 连接完成：
在本地使用 `--config-host 072` 即可读取 072 的配置并自动选择 099。
不复制私钥、不修改服务器认证，也不会在 099 连接失败时改回 072 执行可视化。
两端配置是各自可编辑的副本；每次明确读取哪一份，该份内容才是本轮参数来源，
不会自动覆盖另一端可能更新的配置。

当前机器登记为：072 的 83 器件策略副本，099 的可视化副本。各自二进制、
helper、运行脚本和输入有固定哈希；运行前核验真实 `--capabilities`。
两种二进制不是同一文件，不能把其结果自动称为同二进制对照。
099 尚不支持 83 的旧 Y 拉伸开关；不支持的组合会报错，不静默关闭用户请求的功能。

`hosts.json`、`machine.json`、`bases/` 是机器和案例登记，不是日常调参入口。
两端普通 GETRF 基础配置均继承 84 的 `legacy_components`、距离系数和阶段预算，
仅定位到各自已存在且哈希一致的输入文件。当前登记的完整运行案例
`getrf-prepared` 是 **普通 GETRF、已准备输入、Vivado 10 ns**，不重新综合或导出。
更换设计、器件或后端时钟时，需要登记对应输入；不会把 AMF 目标周期的修改
误认为已经修改 Vivado XDC。

DFX 配置生成仍支持正确的增量键及静态缓存检查；但完整 A/B 增量控制器
还需选用匹配的静态壳、缓存和模块任务，不能用普通 full-run 替代。
本入口会拒绝将 `IncrementalCompilation=true` 送入当前普通 GETRF 案例；
后续增量实验也从同一 `placer.json` 为 A/B 生成参数，并接入对应 DFX 启动器。
本次没有因统一入口重跑或重新生成静态缓存。

每轮提交在目标副本 `experiments/config-dispatch/<name>/` 保存精简配置、
完整配置、基础配置、输入来源、生成器、调度器、解析报告及状态。
实际运行沿用原 `amf3.py full-run`，产物进入新的 `experiments/runs/cfg-<name>-*`；
不覆盖旧结果，DCP 只留服务器。配置中的 `jobs` 同步用于 OMP/MKL/OpenBLAS；
Vivado 保持 4 线程。启动检查保留 60 GiB 可用内存和 20 GiB 磁盘门槛，
资源不足直接拒绝，不降低门槛、不重复派发。相同请求名称不会重复启动。
退出码为 0 只标记 `completed_pending_review`，最终路由、DRC、时序仍需验收。
可视化开启只导出轨迹，不自动启动网页查看器或处理已有 GUI。

## 保留的高级设置

- 84 基础配置中的 `BipartiteMatchingBackend=legacy_components`、距离系数、阶段预算等继续继承；三个 Pro 值现在以精简 JSON 为准。
- SLL 独立分支的 `SllRegionPolicy` 有 `preserve` / `release-single` 两个取值；后者为已实现的实验策略，当前暂不启用。它控制区域偏好，独立于 SLL 数量目标；release-single 取消符合条件单体 PU 的整条偏好，包含锚点牵引和偏好区域裁侧。当前 78/83 不含其解析，因此不注入本精简配置。
- 查看器启动及后处理参数仍见 `docs/research/placement-viewer-user-guide.md`；精简 JSON 只提供可视化导出总开关。
