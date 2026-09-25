# 文件位置与命名规范

本项目所有新增文件放在 `AMFplacer3.0/` 内，父目录不再放散落的 Python/Tcl 脚本、报告和预检目录。服务器正式目录为 `/Projects/jinyang/workspace/AMFplacer3.0`，本地为同名的轻量管理镜像。

| 内容 | 位置 | 命名规则 |
| --- | --- | --- |
| 源码 | `src/` | 保留上游结构，新增模块按其约定 |
| 正式运行入口 | `scripts/` | Python/Tcl 使用 `snake_case` |
| 专项诊断工具 | `scripts/diagnostics/` | `diagnose_<case>_<issue>.py` |
| 主机、实验配置 | `configs/machines/`、`configs/experiments/` | 新名称使用小写 `kebab-case` |
| 需求及其历史副本 | `docs/requirements/`、其下 `archives/` | 主版本 `task-3-final.pdf`，副本须明确标注来源 |
| 调研、说明和参考资料 | `docs/research/`、`docs/references/` | 小写 `kebab-case`，正文可用中文 |
| 完整实验 | `experiments/runs/<run-id>/` | 保留既有实验 ID，新实验由入口自动分配唯一 ID |
| case/工具预检 | `experiments/preflight/` | `YYYYMMDD-<case-or-tool>-<purpose>` |
| 诊断证据 | `experiments/evidence/` | `YYYYMMDD-<case-or-tool>-<issue>` |
| 编译结果 | `builds/<build-id>/` | 由构建入口生成；`current` 为成功构建链接 |
| 本地报告 | `local-reports/<run-id>/` | 使用同步器白名单，不放 DCP |
| 旧脚本 | `archives/legacy-workspace/scripts/` | 原名保留，仅供追溯，不能作为当前入口 |
| 以前下载的 DCP | `artifacts/legacy-downloads/<run-id>/` | 保留已有副本；今后 DCP 默认仅在服务器 |

2026-09-25 已将父目录旧脚本和两个预检目录纳入上述位置。本地重复文档与报告仅在 SHA-256 一致时合并；不同版本保持独立。移动清单记录在 `docs/provenance/workspace-organization-*.json`。

服务器父目录仅保留真实项目目录，以及 `amf-runs -> AMFplacer3.0/experiments/runs` 这一兼容链接。历史 manifest、日志、GDB/CMake 记录中的旧绝对路径不批量改写；原成功实验及最终 DCP 的正式位置不变。历史实验 ID、现有配置文件名及上游源码名不为外观统一而更改。

本地父目录保留工作区 `AGENTS.md` 和 Git 元数据。旧 `research/`、`results/`、`scripts/` 和散落 PDF 已移入项目。

本地与服务器并非逐文件完全镜像。源码、完整实验和构建以服务器为准；本地证据与旧下载文件不会自动上传。同步管理文件前核对两端修改，不使用覆盖整个项目的删除式同步。
