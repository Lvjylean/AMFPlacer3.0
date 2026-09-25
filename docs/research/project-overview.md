# AMFplacer3.0 项目入口

更新日期：2026-09-25。用户正式命名 AMFplacer3.0，在 AMFPlacer 2.0 基础上实现课题三的 FPGA 增量编译器；当前版本 `3.0.0-dev`，本次完成项目初始化与基线归档，增量算法尚未实现。

服务器：`ssh eda072`，账户 `jinyang`。正式根目录：

```text
/Projects/jinyang/workspace/AMFplacer3.0
```

源码、benchmark、独立构建、需求文档、实验登记、成功/失败运行均在该目录管理。旧 `/Projects/jinyang/AMF-Placer` 保持原状。三轮历史实验已迁入 `experiments/runs/`，旧 `/Projects/jinyang/workspace/amf-runs/<id>` 保留符号链接；原始 manifest 的历史绝对路径仍有效。

Git 分支为 `codex/amfplacer3-foundation`，保留原仓库历史，并新增：

- `77365ac9`：保存原工作区的 3 处已修改源码及 2 个 MLTimingModel 文件；标签 `amf2.0-validated-20260925`。所有源码内容与成功基线使用的编译源码快照一致。
- `7ed7e39f`：项目管理框架、需求映射、路线图、实验归档和服务器保存 DCP 的工作流；标签 `amf3.0-foundation-20260925`。

成功全量基线为 `faceDetect-benchmark-20260925-144131`。最终 DCP 的正式路径：

```text
/Projects/jinyang/workspace/AMFplacer3.0/experiments/runs/faceDetect-benchmark-20260925-144131/reports/faceDetect_amf_routed.dcp
```

文件大小 52,697,801 字节；SHA-256：`a0a9c38831cfd171cdf06f2ee93dcedf1e531c1fca344d6a90268c7ca6e85e36`。基线指标在 `experiments/baselines/faceDetect-20260925.json`，详细证据在 `docs/research/face-detect-benchmark-validation.md`。

今后最终 DCP 默认只保留在服务器；本地使用白名单同步器获取报告和日志。此前已经下载的 DCP 不自动删除。本地 `AMFplacer3.0/` 只是轻量管理镜像，完整源码和构建以服务器仓库为准；两端后续同步应先比较差异。

常用命令：

```bash
# 服务器
cd /Projects/jinyang/workspace/AMFplacer3.0
make check
python3 scripts/amf3.py build
python3 scripts/amf3.py run --dry-run
python3 scripts/amf3.py run
python3 scripts/amf3.py status

# 本地工作区
python3 AMFplacer3.0/scripts/sync_reports.py <run-id>
```

初始化验证：本地与服务器的 3 项管理工具测试均通过；新目录独立编译 AMFPlacer 和 partitionHyperGraph 成功；运行入口 dry-run 检查通过；构建二进制哈希一致；服务器 Git 工作区干净，未跟踪 DCP。实际同步成功基线的 24 个文件、3,423,146 字节，本地管理镜像中的 DCP 数量为 0。

新构建为 `build-20260925-152056-398298-7ed7e39f`，入口 `builds/current`。此次项目初始化未再次执行整轮 Vivado 布局布线，完整流程通过的证据来自已归档基线。

需求来源使用用户指定的 `/Users/jinyanglyu/Documents/课题三final.pdf`，服务器存于 `docs/requirements/task-3-final.pdf`。后续开发从 `docs/requirements/traceability.md`、`docs/roadmap.md` 和 `docs/known-issues.md` 开始。
