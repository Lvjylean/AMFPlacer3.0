# faceDetect 已有 benchmark 完整验证

**最终结果：完整布局布线链路通过。** 成功轮次于 2026-09-25 14:52:35 完成，AMF 与 Vivado 均以 0 退出，最终 DCP 已重新打开并核验。

启动日期：2026-09-25 14:32:41。服务器：eda072（ee4e072），账户：jinyang。

本次验证范围：已有 benchmark → AMFPlacer 布局/打包 → Vivado 导入布局和布线 → routed DCP、DRC、时序及资源报告。使用已有二进制；不包含重新导出网表、RTL 综合、增量算法或比特流生成。

## 实验位置

```text
/Projects/jinyang/workspace/amf-runs/faceDetect-benchmark-20260925-143241
```

启动 runner PID：953717。实时状态由该目录的 `status.json` 记录，阶段时间与版本见 `manifest.json`。

使用 `scripts/run_face_detect_flow.py --input-mode benchmark` 创建独立快照，原工程及前一次失败实验均保留。

## 输入及核验

- 整套复制 `/Projects/jinyang/AMF-Placer/benchmarks/testConfig/faceDetect.json` 引用的 11 项输入，校验 SHA-256。
- 使用历史网表、宏、固定位置、时钟、聚类和器件信息，不与新导出文件混用。
- 保留历史宏文件原样，包括现有加载器跳过首行的行为，方便与历史成功记录比较。
- 使用同一 faceDetect routed DCP 的独立副本，后端在内存中清空旧布局并导入 AMF 结果。
- 后端启动时逐一检查 benchmark 单元名称及类型是否与 DCP 对应，并保存参考 DCP 的时序和 DRC 报告。
- AMF 使用 8 个线程；Vivado 2024.2 使用最多 4 个线程。

## 已知 Tcl 适配

现有 AMF 输出的 `$errorNum` 独立一行会被 Tcl 当作命令。保留原输出，在送入 Vivado 的副本中仅将该诊断行改为 `puts`；记录适配前后的哈希和替换次数。布局命令及算法保持原样。

## 启动检查

14:33 已通过先前失败的宏加载阶段，并进入全局布局。此时仍在运行；最终结论需要检查完成状态、路由错误数、DRC 和时序报告。

## 原二进制的收尾段错误与干净重编译

首轮于 14:34:19 以 SIGSEGV 退出，AMF 阶段耗时 96.670 秒。已生成完整的 `DumpCLBPacking-first-0.tcl`，但未到正常结束标记，流水线因此没有启动后端。

用原二进制在独立输出目录复现，调用栈为 `__libc_free(mem=0x1) → ParallelCLBPacker::~ParallelCLBPacker() → AMFPlacer::run()`。这与前一次新宏输入的断言无关。

在首轮目录的 `rebuild/src` 复制当前源码，使用独立 CMake/Ninja 构建目录重新编译。没有修改源代码，也没有复用旧目标文件。配置与编译均以 0 退出；原二进制及失败日志保留。重编译只是排除旧构建问题的验证步骤，不能单凭此步骤断言旧构建不一致就是根因。

使用重编译二进制于 14:41:31 启动新一轮完整验证：

```text
/Projects/jinyang/workspace/amf-runs/faceDetect-benchmark-20260925-144131
```

新 runner PID 为 980443。两轮使用同一套已有 benchmark。新目录 `reports/rebuild_provenance.json` 记录构建来源、原/新二进制哈希及两轮关联。当前源码与二进制对应关系以这份明确的构建记录为准。

14:43:11 重编译后的 AMF 正常完成，阶段耗时 98.676 秒、退出码 0；内部日志显示 `Placement Done (elapsed time: 97.370 s)`，最终 HPWL 为 788012.419649。已生成最终布局 Tcl 和 `PUInfoFinal.gz`，Vivado 后端随即启动。

重编译前后输出的布局 Tcl 逐字节相同（36,696,588 字节，SHA-256 `7f6cb39f981771a831ebf7300c99970a4ee4decd78051edf7b9532b55e7c3cb3`）。因此这次干净构建没有改变所生成的放置结果。它消除了本次复现中的析构崩溃，但旧二进制内部导致非法释放的具体构建历史尚未确证。

## 完整验证结果

| 项目 | 结果 |
|---|---|
| 成功轮次 | `faceDetect-benchmark-20260925-144131` |
| AMF 阶段 | 98.676 秒，退出码 0 |
| Vivado 后端阶段 | 564.033 秒，退出码 0；含 DCP 校验、参考报告、布局导入、布局、布线与结果输出 |
| 两阶段累计 | 662.709 秒，约 11 分 3 秒；不含前面的失败运行、诊断和重编译 |
| DCP 单元名称/类型校验 | 134,450 / 134,450 匹配 |
| 可布线网络 | 113,125 条，全部已布线 |
| 布线错误 | 0 |
| Setup | WNS = 0.300 ns，TNS = 0，失败端点 0 |
| Hold | WHS = 0.030 ns，THS = 0，失败端点 0 |
| 脉宽 | WPWS = 0，TPWS = 0，失败端点 0 |
| 总线偏斜 | 8 项约束均通过，最差裕量 6.875 ns |
| 默认 DRC | 无 Error / Critical Warning；288 个 Warning、40 个 Advisory，规则及数量与参考 DCP 相同 |
| 最终 DCP | 52,697,801 字节，成功重新加载 |

参考 DCP 的 WNS 为 0.302 ns，当前为 0.300 ns，变化 -0.002 ns；两者 TNS 均为 0。不把不同历史运行条件下的约 416 秒与本轮 99 秒直接解释为算法加速。

## 放置导入与最终位置核验

AMF 导出包含 1,431 个初始 `place_cell` 调用，涉及 133,561 个不同单元。初始导入有 188 个失败批次，脚本内重试后 `placementError` 保存了 403 个批次、806 个单元，随后由 Vivado `place_design` 继续完成布局。

在重新打开最终 routed DCP 后，对 AMF 的全部 133,561 个放置请求进行只读核验：

- 全部单元都能找到，未放置单元数为 0。
- 132,523 个单元保留 AMF 请求的位置，占 99.222827%。
- 1,038 个单元的位置经后端调整；这是最终位置对比，不仅限于导入重试清单。
- 27 个带反斜杠转义的单元名在核验前按唯一对应的 benchmark 名称规范化；映射保存在 `placement_name_normalization.json`，未更改布局命令或设计。

因此 `placementError` 是进入 Vivado 正式布局前的重试记录，不代表最终 DCP 仍有相同数量的放置或布线错误。当前流程包含 Vivado 的布局补全/调整，不能把结果描述为完全保留 AMF 每一个指定位置的纯路由流程。

## 结果边界

这是在现有设计约束下的布局布线基线验证。参考设计已有的时钟 methodology 提示、1 个无 input delay 的端口与 3 个无 output delay 的端口仍需在后续设计签核中处理；时序报告的 methodology 小节提示其缓存结果可能不是最新，本次没有另行重新运行完整 methodology 检查。默认 DRC 与所有受现有约束检查的时序通过，不等于完整板级签核。

本次未验证从 DCP 重新导出的宏接口，也未验证增量编译功能。新导出宏的 KEEP 冲突仍是独立待修问题。

## 结果文件

服务器最终 DCP：

```text
/Projects/jinyang/workspace/amf-runs/faceDetect-benchmark-20260925-144131/reports/faceDetect_amf_routed.dcp
```

本地已同步最终 DCP、报告、日志和版本/配置记录：

- [最终 routed DCP](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/results/faceDetect-benchmark-20260925-144131/reports/faceDetect_amf_routed.dcp)
- [机器可读验证汇总](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/results/faceDetect-benchmark-20260925-144131/reports/verification_summary.json)
- [最终时序报告](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/results/faceDetect-benchmark-20260925-144131/reports/timing_summary.rpt)
- [最终 DRC 报告](/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/results/faceDetect-benchmark-20260925-144131/reports/drc.rpt)

DCP SHA-256：`a0a9c38831cfd171cdf06f2ee93dcedf1e531c1fca344d6a90268c7ca6e85e36`。

再次复现时应显式选择已验证的干净构建，默认原工程二进制仍保留用于排查：

```bash
ssh eda072 'python3 /Projects/jinyang/workspace/run_face_detect_flow.py --input-mode benchmark --binary-dir /Projects/jinyang/workspace/amf-runs/faceDetect-benchmark-20260925-143241/rebuild/build'
```

该命令会新建实验目录并执行整轮验证，不应用于只读查询状态。
