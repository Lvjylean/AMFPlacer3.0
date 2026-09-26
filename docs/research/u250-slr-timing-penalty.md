# U250 纵向 SLR 边界延迟修正

日期：2026-09-27。用户要求沿用现有 X 方向时钟区域延迟修正的方式，为纵向 SLR 边界增加更大惩罚；暂不加入 SLL 容量与估计拥塞。

## 模型与参数

```text
新连接延迟 = 原模型连接延迟 + 穿越的 SLR 边界数 × SLRBoundaryDelayNs
```

- 原有三个距离区间的回归公式、X 方向修正（符合原判断条件时每档 0.5 ns）和 0.05 ns 延迟下限保留。
- `SLRBoundaryDelayNs` 默认 **1.5 ns/道边界**，GETRF 全流程配置显式记录为 `"1.5"`。一、二、三道边界分别增加 1.5、3.0、4.5 ns。
- 参数单位为 ns，支持有限非负数；设为 0 关闭新修正，负数、非数值、无穷值或单位后缀会报错。
- 1.5 ns 是比原 X 方向每档修正更大的启发式初值，不是实测 SLL 固有延迟，也未完成参数最优化或正式校准。GETRF 的 `ClockPeriod=10 ns` 未改变。
- 同一 SLR 内跨普通 Y 方向时钟区域不加此项。旧 VCU108 输入缺省为 SLR0，因此新项为 0。

`DeviceInfo` 在读取器件时，根据每个站点的 SLR ID 及 clock-region Y 行建立边界前缀计数。查询使用两行计数差，不把 SLR ID 数值差当作跨界次数，不硬编码 U250 的行号。当前只接受各 SLR 沿纵向堆叠、同一时钟区域行属于同一 SLR 的器件拓扑；不满足时明确报错。SLR 接缝的连续坐标归属沿用现有 clock-region 查询规则，精确落在中点时归下侧。

## 对布局的影响

修正在 `PlacementTimingOptimizer::getDelayByModel_conservative` 中统一生效，两个 `getDelayByModel` 接口均经过此函数。现有 STA、slack 驱动的 QP 连线加权和时序驱动详细布局候选评估都调用这一模型，因此新代价会沿现有路径进入优化，不额外重复乘一层跨界权重。

此项是允许穿越的有限代价，不禁止普通网络跨 SLR；专用 Carry/DSP 级联仍由既有合法性检查约束。不修改坐标、放置密度或站点容量，不模拟 SLL 数量、通道分配与拥塞，也不引入外部 partition/floorplan。

## 验证与构建

构建：`builds/build-20260927-002602-194285-1185c92f/build/AMFPlacer`。由 `scripts/amf3.py build --jobs 8` 建立独立源码快照；manifest 保存起始提交、工作区变更、源码哈希、编译器信息和退出状态。

原生回归目标 `checkSLRTiming` 从同一构建快照编译，调用真实器件读取器、延迟函数及 STA；Python 驱动位于 `tests/check_slr_timing.py`。

```sh
python3 scripts/amf3.py build --jobs 8
cmake --build builds/current --parallel 4 --target checkSLRTiming
python3 tests/check_slr_timing.py --root . --binary builds/current/checkSLRTiming --output experiments/evidence/<新的唯一目录>
```

检查包括 U250 三条接缝与三个距离区间、跨两/三道边界、普通时钟区域边界、反向对称性、中点归属、X/Y 同时跨界、自定义系数与关闭、新旧器件格式、不连续且乱序的 SLR ID、错误拓扑和非法参数。FF→LUT→FF 小网表用于确认边界惩罚进入 STA 到达时间及 slack，测试专用目标为 1 ns，与 GETRF 的 10 ns 配置分开。

验证结果：**13 组原生回归通过，检查 113 行模型/STA 数值；38 项既有 Python 回归通过**。原生运行目录 `experiments/evidence/20260927-u250-slr-timing-0030/`，轻量验收记录 `experiments/evidence/20260927-slr-timing-validation.json`。构建快照中的 3388 个源码文件与当前源树一致。

在原 GETRF 报告出现的 `SLICE_X117Y237 → SLICE_X117Y243` 坐标上，模型从 0.465154 ns 增加至 1.965154 ns。测试 FF→LUT→FF 路径跨界并返回，到达时间从 1.130309 ns 增至 4.130309 ns，slack 从 -0.030309 ns 变为 -3.030309 ns；这些是小网表的原生模型验证结果，并非重新布线后的 GETRF 指标。

AMF 二进制 SHA-256：`5b24bb021c81b69a9cc43853493ec01792cc2045ebcfab29a512f1319ea67959`。

原完整流程验证构建 `builds/validated-getrf-u250-full` 保持原指向；`builds/current` 指向新构建。当前改动尚未重新运行完整 GETRF 布局布线，因此不宣称 WNS 改善或时序收敛。
