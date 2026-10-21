# 常用 placement 配置

日常编辑 `placer.json` 的 16 项参数。输入、器件模型、距离系数、匹配算法和阶段预算来自完整基础配置；精简 JSON 不能直接交给 AMF。
本分支的生成器默认 `--profile combined`，对应同一源码中的器件策略、74q 增量修补和可视化导出。
JSON 初值保持用户当前设置（U250、10 ns、12 jobs、visualization=true），不改算法全局缺省值。

| 键名 | 取值 | 条件与关系 |
|---|---|---|
| `device` | U250 / VCU108 / VU095 / VHK158 | 必须匹配基础输入；VU095 是 VCU108 的别名；VHK158 仅单时钟 LUT/FF pilot |
| `ClockPeriod` | 正数，ns | AMF 目标；不自动改 Vivado XDC |
| `jobs` | 正整数 | 线程数；任务划分算法继承基础配置 |
| `visualization` | true / false | 导出阶段轨迹；false 清除旧坐标 trace；调度开启时选 099 |
| `ProFFClustering` | true / false | 独立 FF 聚类开关 |
| `ProPrimitiveTiming` | true / false | 普通 Pro 保持 false |
| `ProLegacyIOBoundary` | true / false | 仅旧 VU095 模型；U250/VHK158 设 false |
| `BoundaryAwareClustering` | true / false | 新旧聚类选择器；true 要求物理边界模型 |
| `noBlockAware` | true / false | 仅旧路径；true 关闭旧聚类 |
| `BoundaryClusteringStrategy` | slr-hierarchical / cr-slr-hierarchical | 新聚类启用时生效；源码缺省 slr-hierarchical |
| `BoundaryStageMode` | independent-max2 / merged | 两种 hierarchical 策略共用；缺省 independent-max2 |
| `BoundaryRegionClipping` | true / false | 新版偏好区域裁侧；不取消 Pblock 硬约束 |
| `BoundaryWeakAttraction` | true / false | 弱牵引权重，不是全部牵引总开关 |
| `BoundaryAnchorStopOnRegionEntry` | true / false | 进入目标区域是否提前停止牵引 |
| `ClockRegionColumnStretch` | true / false | 旧聚类且 noBlockAware=false；VU095 缺省 true，U250 缺省 false |
| `IncrementalCompilation` | true / false | 必须配套匹配的 DFX/Pblock/静态缓存上下文 |

`BoundaryAwareClustering=true` 自动保证 `PhysicalBoundaryMode=true`，旧聚类与纵向拉伸不生效。
`BoundaryAwareClustering=false` 时新版策略、阶段、裁侧和入口停止参数不生效；由 `noBlockAware` 控制旧聚类。
生成器在 `resolution.json` 记录派生值、不生效项和变更，历史输入不改写。

```bash
python3 scripts/export_placement_config.py \
  --config configs/placement/placer.json --base /path/to/prepared-amf-config.json \
  --profile combined --output experiments/preflight/my-new-config
```

只有本分支新构建才同时支持 `device_placement_policy_schema=1`、`clock_region_column_stretch_schema=1`、`visualization_config_schema=1` 和 `vhk158_sparse_lutff_schema=1`。
生成器只解析配置，运行入口仍须核对实际二进制、输入哈希及后端时钟。可视化总开关不自动启动网页服务。

## VHK158 支持范围

基础配置需 `VHK158LutFfPilot=true`、真实 `clock region geometry file`、`PhysicalBoundaryMode=false`。
常用配置需 `noBlockAware=true`、`BoundaryAwareClustering=false`、`ClockRegionColumnStretch=false`、`IncrementalCompilation=false`、`ProPrimitiveTiming=false`、`ProLegacyIOBoundary=false`。
单时钟与 LUT1–LUT6/FDRE 类型由 AMF 加载后再检查；不宣称支持 VHK158 DSP/BRAM、DFX 或高级边界优化。
全器件稀疏 CR、坐标偏移、资源空洞和单资源列扩散处理只在该受保护路径启用。

## 跨服务器启动与历史登记

`scripts/launch_placement.py` 保留配置选主机、冻结输入、核验哈希、资源门槛与重复提交保护。
`visualization=true` 始终选择 099。默认仅预检，显式 `--start` 才运行。
`--config-host 072` 可从本地读取 072 的配置并跨机提交，不复制私钥或改变 SSH 认证。

**随仓库保留的 `hosts.json`/`bases/` 仍登记已有 83 和 099 冻结二进制，不是本分支的新构建。**
没有把旧二进制哈希改名冒充 combined。要调度新构建，应在独立登记文件中将 target 的 `profile` 设为 `combined`，填入实际 root、binary/helper 路径与 SHA、所需能力以及 flow_files SHA，再核验对应案例的输入与 Vivado 周期。
使用 `--catalog` 选择该登记文件；不要改写历史实验配置。仅生成 combined JSON 时不需要修改登记。
现有普通运行登记是 GETRF 10 ns；不能直接用于 VHK158 或 A/B 增量。DFX 使用匹配的增量控制器。
原登记的详细说明保存在 `docs/research/placement-config-historical-profiles-20261020.md`。

## 高级参数与查看器

`BipartiteMatchingBackend=legacy_components` 等沿用基础配置。
SLL 独立分支的 `SllRegionPolicy=preserve|release-single` 控制布局区域偏好，独立于 SLL 数量目标；`release-single` 是已实现的实验策略，当前暂不启用。
本源码谱系未接入其解析，不在精简 JSON 中注入该键，也不称已实现其效果。
查看器独立位于 `tools/placement_viewer/`，支持 U250/VHK158、器件分类、历史轨迹和仅 DCP 的元数据入口；无轨迹的 DCP 不会凭空生成动画。
