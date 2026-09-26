# AMFplacer3.0

基于 AMFPlacer 2.0 的 FPGA 增量编译器项目，目标对应《课题三final.pdf》子课题 3.2。当前版本为 **3.0.0-dev**：已有可追溯的全量基线，正在实现 U250 多 SLR 适配；跨版本增量编译待开发。

正式工作区位于 eda072：`/Projects/jinyang/workspace/AMFplacer3.0`。源码、构建、大型输入及最终 DCP 保存在服务器；本地保留轻量管理镜像、报告与日志。

## 目录

```text
src/                    AMF 2.0 源码基础及后续 3.0 实现
benchmarks/             现有配套 benchmark、器件数据和原始工具脚本
configs/                主机配置、实验配置
scripts/                构建、实验、状态查询、报告同步与核验入口
tests/                  项目工具及后续增量能力的回归测试
docs/                   需求、架构、路线图、问题和基线证据
builds/<build-id>/       独立源码快照与构建结果，不入 Git
builds/current          最近成功构建的入口
experiments/registry.json 实验登记索引，轻量版本管理
experiments/baselines/   固定基线指标与产物引用
experiments/runs/<id>/   每次运行的输入、日志、报告与 DCP，不入 Git
experiments/preflight/   早期 case/许可证预检，不入 Git
experiments/evidence/    诊断证据，不入 Git
archives/legacy-workspace/ 历史脚本与兼容链接存档，不作为运行入口
local-reports/          本地同步的轻量报告，不入 Git
artifacts/legacy-downloads/ 以前已下载的本地 DCP，不再自动新增
data/                   大型输入和缓存，不入 Git
```

命名与存放规则见 [目录规范](docs/workspace-layout.md)。本项目文件均收拢在项目内；服务器同级 `amf-runs` 仅是指向 `experiments/runs/` 的兼容链接。

## 服务器使用

```bash
cd /Projects/jinyang/workspace/AMFplacer3.0
make check
python3 scripts/amf3.py build
python3 scripts/amf3.py run --dry-run
python3 scripts/amf3.py run
python3 scripts/amf3.py status
python3 scripts/amf3.py status --run <run-id>
```

`run` 默认使用 faceDetect 的整套历史 benchmark，经 AMF 布局和 Vivado 实现输出报告及 routed DCP。默认使用 `builds/current` 的成功构建。它不是 DCP 新导出或增量模式；这些入口需要后续实现与验证。

本地同步某轮轻量结果：

```bash
python3 AMFplacer3.0/scripts/sync_reports.py <run-id>
```

同步器仅允许报告/元数据/日志扩展名，拒绝 DCP 和符号链接，不传输输入、构建或许可证。

U250 GETRF 完整流程使用 `python3 scripts/amf3.py full-run`，后端重试使用 `full-run --placement-run experiments/runs/<completed-amf-run>`。当前修复、验证进展与限制见 [GETRF 完整流程](docs/research/u250-getrf-full-flow.md)。

## 已验证基线

U250 已开放输入检查和独立硬资源分配/合法化，覆盖 URAM 与 Carry/DSP 专用级联 SLR 检查。`amf3.py legalize-resources` 执行本阶段，`amf3.py validate-resources` 用 Vivado 回读部分位置。已新增实验性 `amf3.py full-run` 入口并修复 GETRF 的 SRL/MUX 初始打包与最终 CLB 映射问题，修复版 AMF 全部 856,998 个单元导出已通过，完整 GETRF 布线验收进行中；不接入外部 floorplan，不实现 SLL 优化。输入层记录见 [U250 输入适配](docs/research/u250-input-adaptation.md)，本阶段验证、命令和限制见 [U250 资源合法化](docs/research/u250-resource-legalization.md)。

2026-09-25 的成功轮次为 `faceDetect-benchmark-20260925-144131`：AMF 98.676 秒，Vivado 后端 564.033 秒，113,125 条可布线网络全部完成，路由错误 0；WNS 0.300 ns、TNS 0，hold 违例 0，总线偏斜 8 项通过。约 99.22% 的 AMF 请求位置在最终 DCP 中保留。

该结果证明现有约束下的全量布局布线链路可用，不能作为增量功能、多芯粒优化或完整板级签核的通过证据。详见 `experiments/baselines/faceDetect-20260925.json` 和 `docs/research/face-detect-benchmark-validation.md`。

需求入口：`docs/requirements/task-3-final.pdf`、`docs/requirements/traceability.md`。开发顺序与已知问题见 `docs/roadmap.md`、`docs/known-issues.md`。原 AMF 说明保存在 `docs/upstream-AMF2-README.md`。
