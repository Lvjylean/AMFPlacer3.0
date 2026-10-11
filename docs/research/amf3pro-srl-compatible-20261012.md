# AMF3Pro：SRL 兼容共享 site 版本（2026-10-12）

分支：`amf3pro_SRL兼容`。根目录主代码为**独立阶段、每次 cluster 的 SLR 最多两轮**；默认 GETRF 配置开启弱牵引。SLR/HPIO 合并版也保存在本分支，以 `variants/merged/src/` 中的完整差异文件覆盖主源码后构建，不能仅通过更换 JSON 将主二进制变成合并版。

## 来源与范围

| 项目 | 来源 |
| --- | --- |
| GitHub 发布基础 | `83dac24f4473fd00c8e4a16a506635862500eaef`，1008 功能分支 |
| 根目录两轮算法源码 | eda099，`e7f6ac93b76c0c798d126ee1d66eb326929f1447` |
| 合并版算法源码 | eda072，`447b35b937f03529696b734abefd8ff453162019` |
| 输入／源码 SHA-256 清单 | `docs/provenance/amf3pro-srl-compatible-20261012.json` |

这些实验快照是独立 Git 仓库中的提交；发布分支继承 GitHub 1008 历史，清单保留快照提交及逐文件哈希，不假定快照提交存在于公共仓库历史。

保留既有 U250 坐标、延迟系数、时钟容量、外部 floorplan 初始化接口、Pro 功能、单向边界牵引、QP 数值保护和细粒度计时。普通 Pro 默认 `ProPrimitiveTiming=false`，不启用器件延迟特例。`BoundaryWeakAttraction` 独立于 `DSPCritical`；本分支默认二者均为 true，其意义不同。FF 弱聚拢与边界弱牵引也是不同机制。

本次 SRL 兼容功能包括：

- `SRLMaxPerSLICEM=4`，按 SRL primitive 数计数，三级 SRL 级联计为 3 个。
- 保留独立 PU、PU ID、初始打包、聚类与 SA 输入；合法化临时建立共享 site 的匹配对象，再将目标展开回各个 PU。
- 共享要求控制信号、时钟反相类别、固定属性、BEL、级联和区域条件兼容；最终打包器保留全部成员并检查冲突。
- 不开放与普通 LUT/FF、CARRY 或 LUTRAM 的任意混装，不修改 PU 密度权重。
- 已撤回 MCLB 按需重算，恢复原 MCLB/CARRY 耦合调用；历史 `mclb-own-distance` 配置仅为追溯材料，不是本分支运行入口。

本次发布没有再修改算法实现，也没有加入讨论中的多核并行优化。两轮快照的一处旧测试仍按 DSPCritical 语义传参，已同步合并快照中现有的修正（weakAttraction = !DSPCritical，3 处调用）；只修正测试，运行算法不变。发布整理仅调整默认 full-run 配置、主机配置选择、相对输入路径及 SRL 控制元数据的路径解析／哈希记录，并补充文档和验证工具。

## 主代码与合并版

| 内容 | 两轮主代码 | 合并变体 |
| --- | --- | --- |
| 源码 | `src/` | 共用主源码，加 `variants/merged/src/` 的 12 个差异文件 |
| 阶段 | SLR 最多两轮，然后 HPIO | 先 SLR X 腾空间／裁侧，再 HPIO Y 腾空间／裁侧，随后合并 GP |
| GP 预算 | 保留独立 SLR 预算上限 2 | 只沿用 HPIO 预算与退出条件，不添加额外 SLR GP |
| 牵引 | SLR 阶段 Y，HPIO 阶段 X | 合并 GP 中 X/Y 同时存在 |
| 弱开配置 | `configs/experiments/getrf-amf3pro-srl-share4-10ns.json` | `variants/merged/configs/experiments/getrf-amf3pro-srl-share4-10ns.json` |
| 弱关配置 | 同目录 `getrf-amf3pro-srl-share4-weak-off-10ns.json` | 同目录 `getrf-amf3pro-srl-share4-weak-off-10ns.json` |

公共条件：GETRF/U250、10 ns、无外部 floorplan、AMF 8 线程、Vivado 2026.1 4 线程、repair 导入、legacy 匹配器、相同输入。合并版还保留其原有 capability 声明、诊断标签及测试差异，均在逐文件清单中列出。

## 配置、输入和构建

在独立检出的本分支根目录操作。`AMF_MACHINE_CONFIG` 选择主机，配置中的 `project_root="."` 表示当前源码根目录；不会切换或覆盖正式开发目录。

```bash
# eda072（缺省）
export AMF_MACHINE_CONFIG=configs/machines/eda072.json
# eda099 改用下面一行
# export AMF_MACHINE_CONFIG=configs/machines/eda099.json

python3 scripts/verify_srl_release.py --variant max2
python3 scripts/amf3.py build --profile --jobs 8
```

构建要求 C++17、CMake、Ninja、OpenMP、Boost 开发库及仓库已有第三方依赖。原实验使用本地 Boost 依赖时，通过既有 `AMF_STUDY_BOOST_LIBRARYDIR` 指向对应库目录，不能复制另一台服务器的绝对依赖路径。

GETRF 网表、器件模型、控制元数据及 DCP 不上传 Git。清单记录已验证输入哈希和两台服务器的源路径；将相同文件复制或链接到配置指定的 `data/inputs/getrf-u250-srl-share4/`，DCP 为 `data/reference/getrf-u250/post_opt.dcp`。本次不重新导出、不重跑布局布线。换用其他设计时，必须从其自身 DCP 导出控制元数据：

```bash
/path/to/Vivado/bin/vivado -mode batch -source scripts/diagnostics/extract_srl_controls.tcl \
  -tclargs /path/to/post_opt.dcp /path/to/srl-controls.tsv
```

准备好输入后核验与运行：

```bash
python3 scripts/verify_srl_release.py --variant max2 --inputs
# control 是原实验模式；避免继承此前诊断实验的干预环境变量。
mkdir -p experiments/evidence/srl-release-control
export AMF_MACRO_STUDY_MODE=control
export AMF_MACRO_STUDY_START=1
export AMF_MACRO_STUDY_OUTPUT="$PWD/experiments/evidence/srl-release-control"
export AMF_LEGALIZATION_TRACE=1
export AMF_MATCHER_WORKER_TRACE=1
python3 scripts/amf3.py full-run --profile \
  --config configs/experiments/getrf-amf3pro-srl-share4-10ns.json \
  --run-prefix getrf-amf3pro-srl-share4-max2
```

`full-run` 缺省配置就是上述两轮弱开配置；`run` 仍是历史 faceDetect 入口，不能混用。每轮输出自动建立独立 run 目录。历史配置仍保留历史服务器路径，应按其原实验记录理解。

## 在独立副本使用合并版

```bash
# 从已提交的本分支建立独立工作树；目录必须尚不存在。
git worktree add --detach experiments/preflight/srl-merged/repo HEAD
cp -a variants/merged/src/. experiments/preflight/srl-merged/repo/src/
cd experiments/preflight/srl-merged/repo
python3 scripts/verify_srl_release.py --variant merged
# 依据同一输入清单准备本副本的 data/，并选择对应主机配置。
python3 scripts/amf3.py build --profile --jobs 8
python3 scripts/amf3.py full-run --profile \
  --config variants/merged/configs/experiments/getrf-amf3pro-srl-share4-10ns.json \
  --run-prefix getrf-amf3pro-srl-share4-merged
```

覆盖后是有明确来源的工作区变更，构建入口会保存源码哈希及补丁；不要将主版的 `builds/current` 二进制用于合并版。`verify_srl_release.py` 会验证完整合并源码，不只检查 12 个覆盖文件。环境变量与控制模式沿用上节；共享数据与 DCP 仍只保存在服务器。

## 已完成实验

| 配置 | WNS/ns | AMF 进程/min | GP/min | GP 宏合法化/min | routing/min | 全局布线轮数（含首次） |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 两轮／弱关 | +0.035 | 21.87 | 13.22 | 5.17 | 22.41 | 2 |
| **两轮／弱开，主配置** | **+0.160** | **21.71** | **13.35** | **4.94** | **22.29** | **1** |
| 合并／弱关 | +0.016 | 26.78 | 17.07 | 7.49 | 41.25 | 4 |
| 合并／弱开 | +0.029 | 27.12 | 17.35 | 7.62 | 29.75 | 3 |

四组均完成全量布线，setup/hold 无违例、DRC Error/Critical Warning 为 0，856,998 个实例的原始 LOC/BEL 全部保留，专用级联及 SRL 级联检查通过。上述是冻结算法快照的既有实验，不是本次发布后新跑的结果，也不证明跨设计普遍收益。

完整结果、DCP 地址及时间口径见 `getrf-srl-share4-final-results-20261012.md`；主配置细分时间见 `getrf-srl-share4-max2-weak-on-amf-timing-20261012.md`。AMF 进程墙钟含输入、布局、打包、导出和诊断，不能称为纯布局算法时间。

## 本次发布验收

- 主源码 3,432 个文件中，3,431 个与两轮冻结快照完全一致；唯一差异是上述测试接口修正。算法实现未改变。
- 12 文件覆盖后完整合并源码与其冻结快照一致。
- 发布入口 Python 回归 169 项通过，主机配置选择、相对路径及四份配置矩阵检查通过。
- 两种源码各自的 SRL 共享策略测试、边界层级策略测试重新编译运行通过。
- 11 项服务器输入（含原始 DCP、SRL 控制元数据）SHA-256 核验通过。
- 发布整理未重跑完整布局布线；算法正确性与 QoR 依据为上述冻结快照的已完成四组实验。
