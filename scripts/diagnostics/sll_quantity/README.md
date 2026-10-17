# 2026-10-13 SLL 历史实验脚本

此目录保存实现、GP 验证和 A/C/D 后端实验的原始脚本，便于追溯。它们包含冻结的 eda099 绝对路径，不是新检出仓库的通用运行入口。

| 脚本 | 原用途 |
| --- | --- |
| `prepare_experiment.py` | 建立当时的独立实验和输入清单 |
| `integrate_qp.py`、`finalize_hooks.py` | 一次性修改当时开发副本的源码；发布源码已包含这些改动，不应再次执行 |
| `launch_validation.py`、`run_case.py` | 建立并运行 GP 对照；worker 当时复制到 study 根目录后使用 |
| `analyze_results.py`、`package_evidence.py` | 读取冻结实验，统计并打包证据 |
| `launch_backend_comparison.py` | 串行运行 A/C/D 完整后端并记录输入、二进制、工具版本和状态 |

新实验按 [SLL 发布说明](../../../docs/research/sll-quantity-release-20261017.md) 生成独立配置，使用 `scripts/amf3.py`。不要重用历史报告目录。
