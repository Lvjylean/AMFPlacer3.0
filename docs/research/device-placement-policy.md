# 器件布局策略框架（83）

源自 78 独立副本，保留 74q 增量修补及后续两阶段/CR 锚点分支。
本轮仅拆分器件策略、恢复可配置纵向拉伸，不新增 U250 优化算法，不运行完整布局布线实验。

## 配置

| 键 | 取值 | 含义 |
| --- | --- | --- |
| `device` | `VCU108` / `VU095` | VU095 原版 2.0 Pro 旧聚类策略；两个名称选择同一策略 |
| `device` | `U250` | 保留 78 的当前 U250 行为 |
| `device` | `VHK158` | 预留，尚未实现；布局入口报错，不退回其他器件规则 |
| `ClockRegionColumnStretch` | `true` / `false` | 显式控制旧聚类后的 Y 向列拉伸 |

未指定 `device` 时仍按历史默认 `VCU108`。拉伸开关缺省值：VU095/VCU108 为 true，U250 为 false。
配置值沿用 AMF 字符串格式。新键需要具有 `clock_region_column_stretch_schema=1` 能力的二进制；脚本入口拒绝旧二进制忽略此键。

```json
{
  "device": "U250",
  "BoundaryAwareClustering": "false",
  "noBlockAware": "false",
  "ClockRegionColumnStretch": "false"
}
```

拉伸执行条件为旧聚类路径：`BoundaryAwareClustering=false`、`noBlockAware=false`，且有效拉伸开关为 true。
开启新版聚类时，本键不会启用旧拉伸；设置 true 也不会覆盖 `noBlockAware=true`。
原 `stretchClockRegionColumns()` 密度/比例/固定单元判断及坐标运算保持不变；没有新增拉伸算法或修改经验阈值。

## 器件策略边界

`device` 与 `BoundaryAwareClustering` 是同级配置。后者仍决定旧聚类或新版聚类。
新版 `slr-hierarchical`、`cr-slr-hierarchical`、`BoundaryStageMode`、裁侧与弱牵引不被器件选择自动修改。

VU095 保留旧列 2 / 左列 1 / 右列 3 的 X 牵引规则以及旧侧向限制。
U250 本轮保留现有分支：开启 `experimental multi-SLR placement` 时按选中 CR 列牵引，否则仍走原兼容规则；旧侧向限制也原样保留。
这种保留仅为本次框架拆分的兼容要求，不代表已经完成 U250 的侧向策略重新设计。
未来 U250、VHK158 新规则进入独立器件策略分支，共享网表、QP、合法化和打包实现。

`PhysicalBoundaryMode`、`ProPrimitiveTiming`、距离系数、SA 代价、阶段预算和线程/任务后端不变。
`IncrementalCompilation`、Pblock、静态缓存、引脚限制/LOCK_PINS、时钟 guidance 不由器件选择隐式启停。
VU095 原有聚类/牵引/限制算法保留，不声称本修订的整个程序与原版 2.0 Pro 二进制逐位等价。

## 代码管理

独立目录 `20261020-device-placement-policy-83/repo`，独立分支 `codex/device-aware-clustering`。
Git 基准为 `39464f8f9089821e52ee80107c0ddaf18215fbf7`，但 78 的继承源码含未提交 overlay；
`parent-source-manifest.json` 逐文件记录实际继承源，不能将全部功能归于基准提交。
本轮改动单独列入 `device-policy-changes.json`，保留旧源码、冻结二进制和历史运行。
本轮不提交、不推送；不替换默认构建。后续可把继承基线与本轮器件策略分别审阅/提交。
