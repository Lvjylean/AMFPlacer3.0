# 扩散二维位移限幅修复

日期：2026-09-29。用户授权修复，R10 尚未启动。

生产代码仅修复 src/lib/HiFPlacer/placement/placementInfo/PlacementInfo.h:1099：disY 从 fabs(x-lastSpreadX)*forgetRatio 改为 fabs(y-lastSpreadY)*forgetRatio。启用条件、限幅阈值和二维统一平滑系数保持原样，不增加纵横加权。

上游引入提交：4a5792a46d235100e5e28b3418555aea2e216a6b（2022-02-09）。R09 和之前的候选构建保持不变。

回归调用实际 PlacementUnit 方法，覆盖正负纯 X/Y、3:4 斜向、低于/恰好上限、零位移、零平滑比例及首次无历史位置。旧代码失败 5/10；新代码通过 10/10。测试为 src/tests/check_spread_displacement.cc，CMake 目标 checkSpreadDisplacement。

新构建：/Projects/jinyang/workspace/AMFplacer3.0/builds/build-20260929-131650-338460-c70df682/build/AMFPlacer

SHA-256：7581896c9a32d91433c4ac9bbc1a07c45570dd32d647acfe2282dd573a966619

相对之前待用 R10 源码快照，仅 3 个文件不同：PlacementInfo.h 的一行修复、新测试文件、CMakeLists.txt 的测试目标。完整 AMFPlacer/partitionHyperGraph 构建成功，保留 builds/current 原指向。

对新构建的 checkDelayCoefficients，固定 8,582 组坐标，仅切换共享 y2xRatio=0.71/0.4，基础/边界延迟接口和 4 个 STA 探针输出文件完全一致。共享比例不在非线性延迟回归特征中，仅修改此比例不需要重拟合；仍使用针对当前 tile-columns-v3 坐标的新 U250 系数。真实布局会变化，最终 QoR 仍需完整实验验证。

证据：/Projects/jinyang/workspace/AMFplacer3.0/experiments/evidence/20260929-spread-displacement-fix/manifest.json
