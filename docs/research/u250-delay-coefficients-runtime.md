# U250 新延迟系数接入

2026-09-29，按用户最终确认的范围完成。U250 使用已确定的 `tile-columns-subsites-v3` 坐标，近段保留原参数，中、远段采用本轮推荐值。所有现有延迟调用统一使用对应器件的参数；VCU108/xcvu095 以及其他器件保持原参数。

## 实现范围

生产代码只修改 `PlacementTimingOptimizer.h/.cc`。构造函数根据 `DeviceInfo::getDeviceName() == "U250"` 一次性初始化中、远段系数，系数对象保持只读；近段数组不变。系数编入二进制，校准 JSON 是数值来源与追溯材料，不是运行时加载文件。

`getDelayByModel_conservative` 完整函数以及两个 `getDelayByModel` 接口的源文本与修改前逐字节一致。STA、增量 STA、slack、候选位置评价、打包和其他优化调用点均未修改。没有新增按引脚、扇出、SLR、连接类型或训练距离范围选择新旧模型的分支。

```text
dx = abs(X1-X2), dy = abs(Y1-Y2)
近：dx²+dy² < 9
中：9 <= dx²+dy² < 36
远：dx²+dy² >= 36

d = [c0+c1*(2dx)^0.3+c2*dy^0.3+c3*(2dx)^0.5+c4*dy^0.5]/1000
```

以上系数单位为 ps，结果为 ns。新坐标配置启用物理边界模式，最终结果仍是 `max(0.05,d) + 原物理边界惩罚`。SLR 为 1.5 ns/道，HPIO 为 0.5 ns，均未重新拟合。旧模式中原 X 方向修正与截断的先后顺序也完全保留。

| U250 段 | c0 | c1 | c2 | c3 | c4 |
|---|---:|---:|---:|---:|---:|
|近，原值|95.05263521|-26.50563359|77.42394117|106.29195883|-14.975527|
|中，新值|160.1553292055571|-64.94911625084538|22.807442252015385|120.36632967195294|27.120924804902547|
|远，新值|202.12777470189823|-146.96155453685554|-131.59702340956898|163.46881911227194|136.17523346378107|

C++ 最终使用 float，与原实现精度一致。完整拟合依据仍见 `u250-tile-columns-delay-calibration.md` 和 `configs/calibration/u250-tile-columns-delay-20260929.json`；原 JSON 的候选阶段状态作为历史记录保留。

## 构建与使用

正式服务器根目录：`/Projects/jinyang/workspace/AMFplacer3.0`。

- 构建：`builds/build-20260929-105412-069663-c70df682/build/AMFPlacer`。
- 独立入口：`builds/u250-tile-columns-delay-20260929/AMFPlacer`，链接到上述冻结构建。
- 新坐标配置：`configs/experiments/getrf-u250-tile-columns.json`。
- AMFPlacer SHA-256：`f452a364991dc145e66144a2722f00f99eb949d4badd0bf131624f36c23b804b`。
- 起始提交：`c70df682d8b4bdbe9b71b56008d1087d52ddad51`；构建 manifest 保存完整工作区状态、源码快照、逐文件哈希、工具版本和构建退出码。

后续需要运行新参数时，在服务器根目录指定这对配置与二进制，例如仅运行 AMF：

```sh
python3 scripts/amf3.py full-run \
  --config configs/experiments/getrf-u250-tile-columns.json \
  --binary builds/u250-tile-columns-delay-20260929/AMFPlacer \
  --amf-only
```

本次没有执行以上布局命令。旧冻结二进制和 `builds/current` 指向保持不变；使用旧二进制不会获得新系数。新构建按构建入口快照了工作区中已有的合法化/匹配修改，这些不是本次系数补丁的内容，因此该构建本身不构成与历史布局的严格单变量实验。

器件选择按名称统一生效，不增加坐标版本开关。使用本次系数应配套上述新坐标配置；历史 U250 坐标配置仍保留用于追溯。

## 验证

完整 AMFPlacer 与独立原生探针 `checkDelayCoefficients` 均编译成功。探针使用真实器件读取器、生产时序优化器和 FF→LUT→FF 测试路径；内嵌修改前的完整距离函数，作为 095 的原生回归参照。Python 另用校准 JSON 独立计算新参数预期值。

|检查|测试点|结果|
|---|---:|---|
|U250 新坐标，SLR 惩罚 1.5 ns|8582|全部通过；包含8248个拟合观测、阈值附近、全器件随机位置及边界点|
|U250 新坐标，SLR 惩罚 0 ns|15|全部通过；IO 惩罚仍保留|
|VCU108/095 原生参照|323|与旧函数的 float 返回值逐项完全相等|

U250 与独立数值复算的最大差为 `7.144862550489961e-7 ns`。近段结果与旧模型完全相等；所有测试点的两种调用接口和交换端点结果完全相等。覆盖 SLR3、远超训练距离、跨 SLR/HPIO 的位置仍统一使用新中远段系数，没有范围回退。

每组另检查近、中、远、跨界四种位置的实际 STA 和 slack 传播，共12种路径设置；到达时间增量与两段连接延迟之和一致，slack 对应反向变化。源文本审计确认分段、表达式、截断、边界项和两个接口未改变。

证据在服务器 `experiments/preflight/20260929-u250-delay-coefficients-01/`，本地轻量副本在 `experiments/evidence/20260929-u250-delay-coefficients/`。原生测试驱动为 `tests/check_delay_coefficients.py`，C++ 探针为 `src/tests/check_delay_coefficients.cc`。

这是实现与数值一致性验证，没有新 placement、Vivado route 或 DCP 下载。拟合样本的覆盖和范围外误差结论仍以原校准报告为准；统一应用新系数是本次用户确定的使用方式，不表示已验证全设计预测精度或最终布局收益。
