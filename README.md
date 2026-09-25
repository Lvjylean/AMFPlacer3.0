# AMFplacer3.0

基于 AMFPlacer 2.0 的 FPGA 增量编译器项目，目标对应《课题三final.pdf》子课题 3.2。当前版本为 **3.0.0-dev**：已建立可追溯的全量基线，跨版本增量编译与多芯粒扩展仍在规划阶段。

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
data/                   大型输入和缓存，不入 Git
```

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

## 已验证基线

2026-09-25 的成功轮次为 `faceDetect-benchmark-20260925-144131`：AMF 98.676 秒，Vivado 后端 564.033 秒，113,125 条可布线网络全部完成，路由错误 0；WNS 0.300 ns、TNS 0，hold 违例 0，总线偏斜 8 项通过。约 99.22% 的 AMF 请求位置在最终 DCP 中保留。

该结果证明现有约束下的全量布局布线链路可用，不能作为增量功能、多芯粒优化或完整板级签核的通过证据。详见 `experiments/baselines/faceDetect-20260925.json` 和 `docs/research/faceDetect-benchmark完整验证.md`。

需求入口：`docs/requirements/课题三final.pdf`、`docs/requirements/traceability.md`。开发顺序与已知问题见 `docs/roadmap.md`、`docs/known-issues.md`。原 AMF 说明保存在 `docs/upstream-AMF2-README.md`。
