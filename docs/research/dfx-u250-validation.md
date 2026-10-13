# U250 小型 DFX 首版验收

日期：2026-10-13。首版已在独立代码副本完成小型 LUT/FF 模块的完整 DFX 编译验收。未烧写硬件，不以此声称真实 HLS 或大规模增量编译的性能收益。

## 源码、配置和工具

服务器：eda072。所有本次代码修改及运行位于：

```
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo
```

- 基础源码：确定性子图匹配/网格加速提交 `4dcfefded476cb9571e43e3edb4ff0d7830b8ed2` 的选定文件归档，来源与哈希见父目录 `copy-manifest.json`。
- 独立分支：`codex/dfx-incremental-compilation`；未向正式源码或 GitHub 合并。
- 实验 AMF 源码：`4b31f6f677ad82d66a0eacb5ba533180254c2b9d`。
- 冻结构建：`builds/build-20261013-122624-232823-4b31f6f6/build/AMFPlacer`。
- 二进制 SHA256：`90bf2dacce7e89dc53e299e11a8e401e64546155e5b845916df0c1d1d22a7cf3`。
- 3439 个冻结源码文件与当前副本源码逐文件一致，证据 `experiments/evidence/dfx-source-audit-01/validation.json`。后续修改的是接口检查、后端恢复、报告和测试夹具。
- 开关：`IncremenralCompilation=true`；普通模式缺省关闭。普通 Pro，`ProPrimitiveTiming=false`，12 AMF 线程、网格 batched、`legacy_components`；Vivado 4 线程。
- 器件 `xcu250-figd2104-2L-e`；Vivado 2026.1 Build 6511674；显式目标 `dfx_clk=10.000 ns`。
- `rm_inst` 为活动 RP，`pblock_rm` 为硬 Pblock，示例范围 `CLOCKREGION_X1Y4:CLOCKREGION_X1Y4`。该小型单 CR 用例关闭跨物理边界聚拢；不据此修改 GETRF 的阶段配置。

## 首个配置与替换链

| 运行 | 用途和结果 |
| --- | --- |
| `dfx-u250-generic-shell-04` | 从 RTL `bblock` 属性发现 RP；Vivado 首个配置全流程完成，生成完整/局部 bitstream；将 RP black-box 后锁定静态 routing。 |
| `dfx-u250-incremental-08` | 加载上述静态 shell 和 RM_b，准备及 AMF 均 exit 0；16 个活动 cell 完整导出。后端严格 BEL 一致检查发现 Vivado 在 place_cell 过程中已自动重映射 LUT，保留失败现场。 |
| `dfx-u250-backend-09` | 复用 08 的 linked DCP、AMF 结果及区域/静态清单，未重跑 AMF。允许并审计活动 RM 的合法重映射，静态区继续冻结。最终完整布线、时序、DFX 兼容性及双 bitstream 验收通过。 |

最终后台流程源码提交为 `b7c4380`，精确完整提交、脚本和输入哈希保存在 09 的 manifest。当前文档/交付提交可能更新，但其 AMF C++ 源码与上述冻结构建一致。

夹具增强为 `BSCANE2.TDO = ^sink`，使全部 8 位 RM 输出参与静态观察逻辑；前一版仅观察 bit 0，曾产生 7 条无负载输出。`dfx-u250-incremental-07` 虽已完成全流程，不能作为全接口覆盖验收；保留作工程追溯。本报告采用增强后的 08→09 链。

## 验收结果

| 检查 | 结果 |
| --- | --- |
| 活动 RM | 8 LUT2 + 8 FDRE，共 16 primitive，全部导出并接受导入 |
| AMF 固定上下文 | 19 个静态 fabric primitive 的锚点检查通过；BUFG/BSCAN 留在 Vivado 中 |
| 静态布局/布线 | 各阶段静态 LOC/BEL 检查通过；`pr_verify -full_check` 通过 |
| PR Verify 核对规模 | 1 RP、21 静态 cell、4 静态 site、8540 静态 tile、169 routed node、153 routed PIP |
| 接口 | 17 个逻辑端口（8 入、8 出、1 时钟）名称/方向一致；保存全部 HD.ASSIGNED_PPLOCS |
| 物理 partition pin | Vivado PR Verify 实际比较 1 个；其余数据端口 PPLOC 为空，不能冒称 17 个物理固定 pin |
| 全量布线 | 34 routable net / 34 fully routed net；22 fixed-routing net；routing error=0 |
| DRC | Error=0，Critical Warning=0，Warning=1（HDPR-32） |
| 最终区域约束 | 所有 16 个 RM cell 均在可用 RP site 内；未占用静态或禁止 site |
| RM 位置保留 | 导入与最终均为 12/16 精确 LOC/BEL；最终 15/16 site 相同 |
| bitstream | 完整与局部文件均成功生成，有文件长度及 SHA256 |

4 个活动 LUT 的 LOC/BEL 被 Vivado 原生重映射，其中 1 个换了 site，8 个 FF 均保持原请求位置。这不是静态逻辑被移动，也不能称为 AMF 位置 100% 严格保留。单条 place_cell 命令失败仍中止；流程没有全设计 unplace，没有降低 DRC 等级，没有改变容量或周期。

HDPR-32 指明 U250 不需要显式设置 `RESET_AFTER_RECONFIG`，属于 Warning。PR Verify 另提示参考的首个完整配置未锁定 static；替换输入使用的是该配置 black-box RP 后经过 `lock_design -level routing` 的静态 DCP。比较结果明确 compatible，并核对相同静态 node/PIP；不能把参考 DCP 的 unlocked 提示误写成替换流程解锁了静态区。

## 时序

单位 ns。导入/placed 阶段的时序并非最终实际路由结果。

| 阶段 | WNS | TNS | setup 违例端点 | WHS | THS | hold 违例端点 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 导入完成 | +8.988 | 0 | 0 | -0.203 | -1.603 | 12 |
| Vivado place 完成 | +8.384 | 0 | 0 | +0.048 | 0 | 0 |
| route 完成 | **+9.040** | **0** | **0** | **+0.052** | **0** | **0** |

最终 pulse width slack=+4.725 ns，无 pulse width 违例。这里是少量 LUT/FF 的功能验收夹具，不是 GETRF，不用其大时序余量评价 placer 的实际 QoR。

## 耗时

| 项目 | 秒 | 口径 |
| --- | ---: | --- |
| 首个静态配置完整流程 | 228.571 | shell-04 runner 墙钟，含综合/布局/布线/审计/双 bitstream/静态 DCP |
| 替换模块准备 | 59.258 | 08 的 prepare 进程：DCP 打开、RM 替换/opt、提取与报告等 |
| AMF 进程 | **11.008** | 08，exit 0；09 复用，不重复执行 |
| AMF 初始化 | 6.648 | profiler 中 AMFPlacer 构造函数，主要为全器件模型加载 |
| AMF run() 段 | 2.856 | 含布局调度、导出和诊断，不冒称纯算法时间 |
| AMF 清理 | 1.392 | 析构函数 |
| 后端加载 linked DCP | 19.277 | 09 Tcl 实测 |
| Vivado place_design | **115.296** | 09 Tcl 实测 |
| Vivado route_design | **12.483** | 09 Tcl 实测 |
| 三次阶段审计合计 | 20.296 | imported/placed/routed |
| 三份阶段 DCP 写出合计 | 0.572 | imported/placed/routed |
| pr_verify | 15.588 | full_check |
| 完整 bitstream | 60.927 | write_bitstream |
| 局部 bitstream | 19.953 | write_bitstream -cell |
| 后端续跑 runner 墙钟 | **268.743** | 09，从其独立计时点到结果/哈希写出 |
| 成功链的分段累计 | **339.008** | 59.258 + 11.008 + 268.743；不是一次不中断实测总墙钟 |

08 最初失败的导入检查另耗时 23.235 秒，不计入上述成功链累计；人工诊断和两轮之间的间隔亦不计入。09 后端仍包含 Vivado 固定开销，且保留静态逻辑参与完整时序上下文；当前不能据此声称端到端速度优于同一目标设计的全量实现。

## 服务器产物

以下均在 eda072；DCP 和 bitstream 未下载到本地。

锁定静态区：

```
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo/experiments/runs/dfx-u250-generic-shell-04/static_locked.dcp
```

最终产物目录：

```
/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo/experiments/runs/dfx-u250-backend-09/
```

| 文件 | 含义 | SHA256 |
| --- | --- | --- |
| `imported.dcp` | AMF 导入后 | `1050e151beba04830eab06d975f669fdc3d56bdbc5e3dd9ad695bb1325ce324e` |
| `placed.dcp` | Vivado 布局后 | `d0d922b88f29bfc52090fe2378732f58b45ae351e785a1e2b3977d4d1622dc22` |
| `routed.dcp` | 最终布线后 | `f811a0f4de328cec564cb26ab11bd7b523ec0e15bdc0bd67f5c27236ad5f0144` |
| `full.bit` | 完整配置，113318586 B | `28a46fad1f88fd4249bf0e75ef7e2cef93a269ebfd97ce215b1145b778029d4e` |
| `partial.bit` | `rm_inst` 局部配置，1169779 B | `75a5182dfe4b083806f071eeef4e7c2a67e131c42bc4a3a6cffa420dce2b73b2` |

## 测试与限制

C++ 开关/清单/静态 membership 测试和 6 项 Python/Tcl 数据边界测试通过，证据 `experiments/evidence/dfx-contract-tests-03`。已验证默认关闭不读取 DFX 文件；尚未用本轮二进制重新执行完整 GETRF 关闭开关的 QoR 回归。

实现及后续阶段见 `dfx-incremental-compilation.md`。首版单活动 RP；大型 HLS、多个 RP、复杂硬资源、精确 partition-pin 时序预算回注及硬件动态重配置未验收。AMF 使用保留的静态 fabric 连通性/坐标估计时序，最终边界时序由完整 Vivado 上下文验证。
