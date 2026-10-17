# 全量 AMF 与并行增量 DFX：功能补齐及验收记录

更新：2026-10-14 03:10 HKT。GETRF 大规模验证已启动，尚未得到性能比例。

## 比较口径

全量参考必须是 **AMF 放置完整 V1 设计，再由 Vivado 对完整 V1 布线**。
增量分支从已完成布局布线的 V0 shell 出发，固定静态布局和布线，AMF 只放置修改后的动态模块，Vivado 只处理动态模块布线；两个模块独立完成后回装成完整 V1。

之前误启动的原生 Vivado 全量参考已取消，现场保留，不能用于本轮 AMF 增量加速比。

- 分别判断 AMF placement、Vivado routing，增量时间不得超过对应全量时间的 70%。
- AMF placement 使用细粒度 `placement_orchestration`，扣除导出；进程总时间单列。
- Vivado compatibility placement、输入提取、格式转换、检查、DCP/bitstream 写出单列，不混入 AMF placement。
- 两个并行任务报告各自用时及并行经过时间，不把并行 CPU 工作量之和当作用户等待时间。
- 若只有 AMF 进程时间跨度且其已小于 70%，它可作为 placement 的保守上界；若超过，则不能仅凭这个上界判定 placement 失败。
- 全量与增量必须绑定同一 V1 输入、Pblock 几何、AMF 二进制、模型和配置。缺失证据标为不可评估，不能默认为通过。

## 已补齐的功能

### 完整设计上的多 Pblock 硬区域限制

新增 `HardRegionConstraints`。全量 AMF 可同时放置静态区域及多个 RP，而不是只能对单一 RP 运行局部布局。

```json
{
  "IncremenralCompilation": "true",
  "DFX placement mode": "full",
  "DFX regions file": "hard_regions.tsv",
  "DFX region sites file": "hard_region_sites.tsv"
}
```

层级 membership 决定 cell 所属区域；由 Vivado Pblock 导出的精确 site 集合决定合法性。连续布局用区域包围盒引导，最终合法化和打包必须通过精确 site mask。

- 初始打包保留宏及相对偏移，禁止把不同区域的可选 LUT/FF 等组合成同一 PU。
- 必须整体保留的级联跨越多个 RP 时明确报错，不静默拆断。
- 连续坐标更新约束整个 PU 的包络，静态逻辑避开 RP。
- 宏候选搜索、连续 site 检查、CLB 合法化、SRL 共享 site、最终打包和交换统一检查区域归属。
- 不启用该模式时，原有全量流程保留；局部增量仍使用已有固定静态上下文路径。

涉及 `InitialPacker`、`MacroLegalizer`、`CLBLegalizer`、`PlacementInfo`、网格更新及最终 CLB 打包器。

### 全量 AMF → Vivado 与增量回装

新增 `scripts/run_dfx_full.py` / `amf3.py dfx-full`，以及 `dfx_full_prepare.tcl`、`dfx_full_input.tcl`、`dfx_full_backend.tcl`：

1. 读取经过哈希验证的综合 DCP，建立 RP 和静态区域的硬 Pblock / routing containment。
2. 导出全设计 fabric 网表和区域约束，运行正常 AMF 全局布局、合法化和打包。
3. 验证 fabric cell 完整导出及区域归属，再导入 Vivado。
4. 单独计时 Vivado compatibility placement；完成全量 routing、DRC、时序报告、完整/局部 bitstream。
5. V0 额外生成各 RP 的 locked shell，供已有并行增量 worker 使用。
6. 两个 routed RM 回装后执行 `pr_verify -full_check`；回装本身不再次执行 placement/routing。

fabric 覆盖与 BUFG/BSCAN 等专用时钟/配置原语分开记录，常量也单独记录。专用基础设施由 Vivado 处理，不能声称所有 primitive 均由 AMF 放置；不认识的 primitive 明确失败。

新增有限依赖链 `run_dfx_full_pipeline.py`：V0 全量成功后才启动两个增量 worker，失败即停止后续步骤，无自动重试、无定时检查。

### 输入与验收的遗漏防护

- 全量与增量的 V1 设计、区域约束、二进制和 AMF 设置都有独立的比较哈希。
- 每个增量 RM 校验预期 DCP 哈希，避免错用旧版本。
- 导出、导入和布线后分别保存区域/覆盖检查以及 LOC/BEL 保留情况。
- 大 site 集合的 Tcl 查找改为数组并批量取属性，避免字典在 Vivado 内反复转换；小型用例输入导出约 2.1 秒。
- 099 的模型及 Boost 运行依赖使用独立部署目录；二进制与 072 完全相同。

## 已完成的验证

| 验证 | 结果 |
| --- | --- |
| C++ 编译、独立硬区域约束回归 | 通过；区域重叠、归属、PU 包络及禁用模式有检查 |
| 性能验收脚本 | 14 个合成证据测试通过，覆盖错误参考和证据缺失等情况 |
| 基础双 RP 全量 AMF（072） | 51 个 fabric cell 全部导出，区域违例 0；46/46 routable nets 完成；WNS +7.993 ns |
| CARRY/BRAM/SRL 双 RP 全量 AMF（072） | 143 个 fabric cell 全部导出；139/139 nets 完成；WNS +6.112 ns |
| CARRY/DSP/BRAM/SRL 双 RP 全量 AMF（099） | 145 个 fabric cell 全部导出，含 2 个实际 DSP；155/155 nets 完成；WNS +6.107 ns |
| 基于 AMF V0 的两个并行增量任务及完整 V1 回装（072） | 两个 worker 成功；回装 `pr_verify -full_check` 通过；46/46 nets 完成；WNS +7.889 ns |

以上布线结果的 DRC Error / Critical Warning 均为 0，完整与两个局部 bitstream 均已生成。小型增量测试启用了完整静态 placement 审计；GETRF 保留此前用户选择的跳过昂贵逐 cell placement 审计设置，仍须通过最终 `pr_verify -full_check`。

这些是功能验证，不是 GETRF 大规模时序或 70% 加速比例的证明。072 硬宏测试的初版 DSP 被综合优化掉，DSP 覆盖仅以 099 的实际 primitive 清单为证据。

## 构建与当前 GETRF 任务

开发仅在独立副本：

```text
eda072:
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo

eda099:
/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo
```

基础提交 `b5322e1fb5ff3b862c00a8b97bc963f84494ff70` 加本轮改动；不能把基础提交本身当作已经包含本轮实现。
冻结构建 `build-20261014-023517-806311-b5322e1f`，AMF SHA256：

```text
e097c317b7fff66e3ddd1a5291c86d1a49f7b1bfa589f96d181c9be73217df90
```

冻结源码与当前副本核对 2142 个 C/C++ 文件一致，源码差异与脚本哈希保存在 evidence。099 部署复用此二进制及相同模型。

统一 10 ns，普通 Pro，`ProPrimitiveTiming=false`，AMF 12 线程，Vivado 2026.1 / 4 线程，两个动态区域分别为 `CLOCKREGION_X2Y1`（SLR0）、`CLOCKREGION_X2Y5`（SLR1）。

| 服务器 | 任务 | 独立运行目录（相对上述 repo） | 启动 PID |
| --- | --- | --- | --- |
| 072 | AMF V0 完整布局布线，生成 shell | `experiments/runs/getrf-full-amf-contained-v0-072-01` | 依赖链 1430023 |
| 072 | V0 成功后并行更新 A/B，回装 V1 | `experiments/runs/getrf-full-amf-contained-incremental-072-01` | 待上游完成 |
| 099 | AMF V1 完整布局布线，作为性能参考 | `experiments/runs/getrf-full-amf-contained-v1-099-01` | 1193201 |

任务于 03:08 HKT 启动并确认进入 prepare。072 依赖链状态为 `experiments/evidence/static-containment-01/full-amf-pipeline-072-01/status.json`；统一入口索引为同级 `launch.json`。DCP 和 bitstream 仅保留在各运行服务器。

## 尚未证明或支持的范围

- GETRF 大规模运行仍需验证合法化收敛、完整 routing、时序及增量时间占比。
- 连续布局仍使用包围盒引导，未新增逐区域密度模型；精确 site mask 负责最后的硬合法性。任意不规则/不连续 Pblock 的布局质量没有由本轮小型测试证明。
- 不允许重叠、嵌套 RP，必须整体保留而跨 RP 的宏也会拒绝。
- 静态 containment 不能被表述成“RP 内完全没有任何静态相关互连”：时钟、常量和接口仍需按 DFX 规则检查。全量 routed shell 的实际资源审计和最终兼容性检查仍然必要。
- 本轮没有更改器件延迟系数、牵引参数或周期，也未修改正式项目源码、上传 Git 或恢复定时检查。
