# E2：多物理出口与接收端分组 SLL 约束修复

## 范围与版本

用户允许 Vivado 在导入约束时自动选定源出口，AMF 不指定普通出口。此次修改在新的独立 E2 副本中完成，保留原 E2 的 SLL 规划、cell 位置和 10 ns 约束；不重新执行 AMF，不对 GETRF 执行 `route_design`。

- 服务器副本：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo`
- 分支：`codex/e2-sll-free-terminal`
- 基础提交：`64a4660c8fd628af6864a8594a90f3773cee7d86`
- 来源副本：`experiments/preflight/20261028-e2-sll-dense-density/repo`
- 本次只改 Tcl 导出器、审计和诊断脚本，不改 C++、不重编译 AMF。
- DCP 仅留服务器。本地证据位于 `experiments/evidence/20261028-e2-sll-free-terminal/`。

## 原因与处理

旧导出器把一个逻辑驱动引脚等同于一个物理 site 输出节点。GETRF 的一个 LUT4/O 映射到两个物理出口，因而在设置任何 SLL 约束前被拒绝。这是导出器的表示限制，不是 LUT4 有两个逻辑输出，也不是一次已执行的布线失败。

新实现保留源出口候选集合：

1. 只有一个物理出口时，以该唯一出口连接远端树形约束。
2. 多物理出口、一个顶层 crossing 子树时，省略源出口，把子树交给 Vivado 导入；Vivado 自动补入出口。
3. 多物理出口、多个顶层 crossing 子树时，先导入第一个 crossing 子树，由 Vivado 解析出口，再将全部指定 crossing 子树连接到该出口。没有选择候选数组的第一个出口，也没有使用“离 SLL 最近”等 AMF 规则。
4. 所有源的本地接收端不写入物理 `FIXED_ROUTE`，允许普通布线另选出口；它们仍保留在完整连通性校验和逐接收端 SLL 路径审计中，预期路径为空。

这里的出口自由度是 **AMF 不预先指定，允许 Vivado 导入时选定**。它不是一直保留到 `route_design` 的多选出口约束。多顶层分支会使用原生解析出的共同出口；尚未主张这是延迟最优出口。

普通布线段使用 `GAP`，固定内容仍是 SLL 的精确有向段及接收端分组。没有设置 Pblock 或普通布线区域限制。保持 LOC/BEL，并从真实 BEL 引脚映射生成 LUT 的 `LOCK_PINS`，避免把 site 输入名称误当作 BEL 输入名称。

GETRF 随后暴露出接收端的同类限制：`cols_112_U/ram_reg/ADDRBWRADDR[6]` 同时映射到 `ADDRBL6_PIN` 和 `ADDRBU6_PIN`。新实现将一个逻辑接收端映射的物理输入全部展开为同一个 SLL 子树下的叶子，保留一条逻辑接收端记录，不任意丢掉其中一半。

另一种情况来自 Unisim 转换：原始 DSP 宏接口 `DSP/A[6]` 的 `IS_ORIG_PIN=1、IS_LEAF=0`，没有 BEL/site pin；真实叶子是同一宏内的 `DSP/DSP_A_B_DATA_INST/A[6]`。按 cell 查询、完整名称查询和全层次过滤均返回原始宏接口，因此不是单纯更换查询语法即可解决。新实现沿该 pin 的真实 net 查找宏内、方向一致的物理叶子，不硬编码 DSP 子单元名称，也不将无映射的宏接口误认作未使用输入。源端、接收端、连通性校验及路由审计均使用这一映射；逻辑接收端记录和 SLL 分组保留，物理叶子共享但分组矛盾时拒绝。映射另存为 `sll-macro-pin-aliases.tsv`。

多个逻辑 FF 的 CE/SR 也可能共享同一个物理 site 输入。新实现将同一 SLL 路径下的重复物理叶子合并，但不删除逻辑接收端；同一个物理节点若要求不同 SLL 路径则拒绝。否则约束虽然可导入，`route_design` 会报 `Route 35-3477`（同一路由节点重复使用）。这一失败已由专门的小样例复现并修复。

GETRF 第 5 轮预检继续发现 CARRY8/O[2] 到同 site FF/D 的内部连接。此类接收端没有外部 site 输入，不能要求它提供互连节点。最终导出器对全部本地接收端保留逻辑记录、位置与连通性校验，但不生成外部路由 waypoint；跨 die 接收端才展开物理叶子。路由审计若遇无外部输入的接收端，必须同时满足 net 已完整布通、源和接收端同 site、接收端物理 BEL pin 映射属于同一 net，再将其识别为内部连接；不同 site、无物理 BEL pin 或错误 SLL 分组仍然拒绝。

## 审计

审计从该逻辑驱动的全部实际使用出口出发，读取已布线 PIP 图，核对：

- 整条 net 的实际 SLL 集合与计划完全一致；
- 每个接收端的有序 SLL 序列与其分组一致；
- 本地接收端没有额外跨 die；
- 多条路径汇合时没有互相矛盾的 SLL 序列，不能由“首次到达就停止”的搜索掩盖绕路；
- net 路由完整、没有未完成 stub。

输出分别记录候选出口、导入时选定出口和实际布线使用的出口。仅检查 site pin 的 `IS_USED` 不足以认定它是互连路由树的源节点，因此实际出口还须位于该 net 的布线节点及 PIP 图中。

大型网表查询改为建立 primitive cell 名称索引，再在所属 cell 的少量引脚中查找，并缓存结果。原始逻辑 pin 到物理叶子的结果也缓存；LOC/BEL 批量读取，避免逐接收端重复查询所属 cell。路由后审计和 DCP 重开时重建索引，避免持有过时对象。依据：[AMD UG903：Optimizing Pin Queries](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/Optimizing-Pin-Queries)。

## 验证记录

测试使用 Vivado 2026.1、U250、10 ns 时钟。小样例每个 case 均从未布线模板独立执行默认 `route_design`，不增量布线。

| 小样例 | 接收端分布 | 关键覆盖 |
| --- | --- | --- |
| LUT 本地＋近端＋远端 | SLR1/2/3，各 2 个 | 多出口、一到两次跨界、共享上游 SLL |
| LUT 全部远端 | SLR2 4 个，SLR3 2 个 | 多接收端、连续跨两道边界 |
| FF 下方＋本地＋上方 | SLR0/1/2，各 2 个 | 单物理出口原有行为 |
| LUT 下方＋本地＋上方 | SLR0/1/2，各 2 个 | 多出口、两个顶层 SLL 子树 |
| LUT 同一远端 SLR 分两组 | 本地 2 个，SLR2 两组各 2 个 | 分组分别使用不同的精确 SLL |
| LUT＋BRAM 双物理输入 | 前述本地/近端/远端 6 个 FF，加 SLR2 一个 BRAM 地址输入 | 一个逻辑接收端的两个物理输入属于同一 SLL 分组 |
| LUT→DSP＋FF | SLR1 的 DSP A[6] 和本地/近端/远端 FF | 原始 DSP 输入到物理叶子映射 |
| DSP→本地/近端/远端 FF | DSP P[24] 驱动 6 个 FF | 原始 DSP 输出到物理叶子映射 |
| 同 site 共享 CE | 6 个逻辑 CE，对应 3 个物理输入 | 同组物理节点去重，保留逐逻辑接收端审计 |
| LUT→同 site FF＋跨 die FF | 一条内部接收支路与远端分支共存 | 内部接收端无外部节点时仍完整审计 |

`validation-02`：5 类全部通过，共 30 条接收端路径匹配；DCP 回读后仍匹配；5 次故意改错接收端分配均被拒绝。包含本地接收端的 LUT 样例实际同时使用 HMUX 和 H_O，跨 die 分支选定 HMUX 没有强制本地接收端也使用它。此为约束与路由拓扑验证，不是小样例频率达标结论。

`validation-03`（第一版查询优化）：5 类再次全部通过，运行 333.714 秒；规范化 `FIXED_ROUTE`、导入出口、实际出口和逐接收端路径与 `validation-02` 逐文件一致。30 条路径及 DCP 回读、5 项负对照全部通过。模板未绑定外部端口的警告保留在日志中；这里验证的是目标内部 net 的 `ROUTED` 状态和精确路径，不是整设计 I/O 或 bitstream 验收。

`validation-05`（增加接收端多物理输入支持）：BRAM 样例通过，运行 83.239 秒。7 条逻辑接收端路径及回读通过，负对照被拒绝；另外从实际 PIP 图独立追踪两个 BRAM 物理输入，二者均经过 `LAG_LAG_X78Y530/UBUMP9`。

`validation-06`（增加复合宏接口映射）：DSP 作为源和接收端两类新增样例，以及多顶层 SLL 子树、BRAM 双输入两类回归全部通过；4 次独立完整布线共 335.053 秒，DCP 回读和负对照均通过。

`validation-08`（共享物理输入去重）：6 个逻辑 CE 对应 3 个物理节点，83.444 秒完成独立布线；6 个逻辑接收端的路径、DCP 回读和负对照均通过。纯 Tcl 单元验证另外确认：同组去重后逻辑接收端不丢失，而同一物理节点的不同 SLL 分组会被拒绝。

`validation-10`（内部接收端）：81.638 秒完成独立完整布线，目标 net 为 ROUTED；同 site HFF/D 没有外部路由节点，但映射在已布通 net 的 BEL 集合中。6 个逻辑接收端路径、DCP 回读及错误分组负对照全部通过。

查询计时 `query-profile-01`：固定 GETRF 布局上的前 3 条 net 共 7.114 秒（含 cell 索引），`get_cells` 约 0.517 秒、批量 `get_property` 约 2.668 秒、所有 `get_pins` 合计 0.001353 秒。不能将此小规模时间外推为全量导入时间；冻结计划前 113 条 net 就有 76,901 个接收端。

GETRF 全量约束导入与保存回读通过，最终运行目录为 `/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-sll-terminal-import-20261028-06`。本轮不重新运行 AMF，不重新布局，也没有执行 GETRF `route_design`。

| 检查项 | 结果 |
| --- | ---: |
| 导入 FIXED_ROUTE 的 net | 8,442 / 8,442 |
| 计划并写入约束的 SLL crossing | 8,467 |
| 保留的逻辑接收端记录 | 280,438 |
| 本地 / 远端接收端 | 156,106 / 124,332 |
| 保存后重开、FIXED_ROUTE 完全一致 | 8,442 / 8,442 |
| 原请求 cell 的 LOC/BEL 匹配 | 856,998 / 856,998 |
| 多物理出口驱动 net | 836 |
| Vivado 时钟约束 | 10 ns |
| GETRF 布线 | 未执行 |

出口处理方式：

- `unique_physical_access`：7,606 条 net。
- `vivado_implicit_source`：831 条 net。
- `vivado_import_selected_source`：5 条 net。

本轮验证进程墙钟为 **1278.209 秒**；阶段计时如下。这包括端点校验、约束写入与 DCP 读写，不能当成 AMF 布局时间，也不能据此得出相对 E1 的布局耗时结论。

| 阶段 | 秒 |
| --- | ---: |
| `open` | 102.158 |
| `place_audit` | 22.681 |
| `sll_constraints` | 890.689 |
| `placed_checkpoint` | 57.024 |
| `placement_reports` | 42.294 |
| `checkpoint_constraint_readback` | 131.600 |
| `checkpoint_placement_readback` | 23.317 |

最终约束 DCP 仅保存在服务器：

`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-sll-terminal-import-20261028-06/reports/getrf_placed.dcp`

SHA-256：`9ccb0d061876d493aada21aae90ef7f17aaac7a1052d41e8a6597afc19df5521`。已验证 ZIP CRC、文件稳定性、输入哈希未变以及检查点回读。源出口选择和 DSP 宏映射报告在同目录的 `sll-source-selection.tsv`、`sll-macro-pin-aliases.tsv`。该主运行中的 `sll-fixed-route.xdc` 是修正文本序列化前的历史文件；最终独立加载文件见下述回放验证。

导入后这 8,442 条 net 的 `ROUTE_STATUS` 均为 `ANTENNAS`；本次没有执行路由完成步骤，不能把约束写入成功称为已经布通。

本次的 8,467 是**计划并导入的 crossing 数量**，不是 GETRF 实际布线后使用的 SLL 数量；未测试 GETRF 的可布线性、实际接收端路径或 WNS。


## 独立 XDC 回放与最终导出文件

内存中传给 `set_property` 的 Vivado net 对象，在写成纯文本后不能直接当作同一对象使用。独立回放抓到了 `Common 17-161`：部分 net 名称作为裸对象参数无法解析。最终导出器增加 `constraint_command`，用 Tcl list quoting 保持树结构与 net 名称，显式生成 `set_property FIXED_ROUTE … [get_nets {…}]`。新增单元验证检查包含 `[]` 的 net 名称和嵌套树在命令求值后完全保留。

本项修复只改变导出命令的对象查询写法，不改变 native 导入、SLL 树内容或最终 DCP 中的约束。最终文件通过修复后的同一个导出函数，从已验收主运行的 8,442 条 route/net 参数生成，原文件保留，不覆盖历史数据。

最终独立 XDC：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/corrected-export-01/sll-fixed-route.xdc`。

`getrf-sll-export-replay-20261028-03` **通过**：目标 net 的非空 FIXED_ROUTE 从 0 变为 8,442，10 ns 时钟保持不变，没有路由，没有写入任何 DCP。整进程用时 203.908 秒，其中实际加载 XDC 用时 74.716 秒。输入文件及最终 helper 哈希均复核通过。

最终 XDC SHA-256：`26050c09f8b36878401c1fe93c2494faccc23cf6e6b3f340d767509ff8939183`。

版本界限：主运行 `...-06` 的 native 导入与 DCP 回读对应 `source-snapshot-v3.json`；最终 `source-snapshot-v4.json` 仅追加 XDC 对象序列化修正及回放诊断。最终输出文件使用 v4 的同一个导出函数生成，并完整独立加载验证；未为这一文本修正重复执行 AMF 或 GETRF 布线。

DCP 包含导出器设置的端点 LOC/BEL/LOCK_PINS 属性。独立 XDC 回放验证限定在同一冻结布局，不能据此主张它能直接套用到其他布局或代替布线后审计。

## 已排除的写法与保留记录

- 直接将多个不连续分支放在顶层，或以 `GAP` 作为分支点：即使属性可写入，也会在路由时因 `Route 35-562` / `35-255` 失败。
- 将 `LOGICAL_DRIVER` 当成未解析的 `FIXED_ROUTE` 根占位符：导入时报非法树结构，不能因为实际 `ROUTE` 中出现该标记就将它直接用作约束。
- 旧版 GETRF 验证 `...-01` 已完成布局恢复和检查点保存，856,998 个请求 LOC/BEL 全部匹配；随后因用户允许原生导入选出口而主动结束旧版预检。
- `...-02` 在端点预检阶段因全设计 pin 重复搜索开销而主动结束，随后改为按 cell 索引查询。两轮均没有运行 GETRF 布线，不记作路由失败。各自 `superseded.json` 记录原因；原日志和 manifest 保留。
- `...-03` 在第 113 条计划记录处发现 BRAM 地址输入映射为两个物理输入，旧的接收端唯一节点检查拒绝了它；随后增加同组展开并完成上述 BRAM 实际布线验证。
- `...-04` 运行 2,229.809 秒，在下标 4,592 的记录处因上述 DSP 原始接口无物理映射而停止。此时仍在全量预检阶段，没有写入部分 SLL 约束作为成功结果，也没有运行 GETRF 布线。随后完成 DSP 查询诊断及 `validation-06`。
- `...-05` 运行 2,165.071 秒，在下标 5,372 的记录处发现 CARRY8→同 site FF 的内部接收端无外部节点，仍在预检阶段停止，未执行 GETRF 布线。随后统一移除本地 receiver 的物理 waypoint，并保留内部连接审计。
- `getrf-sll-export-replay-20261028-01` 把空树的非空返回文本当作已有固定路由，前置检查误报；归一化后实际非空旧路由数为 0。后续回放明确在临时内存中清除目标属性，再验证从 0 到 8,442 条的加载，输入 DCP 不改写。
- `getrf-sll-export-replay-20261028-02` 暴露裸 net 名称导出导致 `Common 17-161`，据此修正独立 XDC 的对象查询。此前主运行直接使用 Vivado 对象导入和 DCP 回读的成功结果仍有效。
- `validation-04` 因诊断脚本将 site type 名称当作 RAMB36 而未选到 site，未运行布线。`validation-05` 使用实际 RAMB36 site 名称修正此测试脚本错误。
- `validation-09` 的多顶层 LUT、单出口 FF 两类回归通过；内部接收端样例实际路由完成，但 `BEL_PIN.IS_USED` 返回 0，被新增审计错误拒绝。改用已布通 net 的 BEL 映射、同 site 位置及无外部输入联合判定；不再将该单一属性当成内部连接的必要条件。
- `internal-access-01` 识别出第 5 轮失败记录的 CARRY8 源与内部 FF，但诊断脚本试用了不支持的 `get_bel_pins -uphill`，因此该诊断不作为路径连通证明。
- `validation-07` 复现共享 CE 节点被重复写入树后，导入可接受、实际路由报 `Route 35-3477` 的失败；`validation-08` 验证物理叶子去重修复。

约束前 GETRF 检查点：`/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261028-e2-sll-free-terminal/repo/experiments/runs/getrf-sll-terminal-import-20261028-01/reports/getrf_before_sll.dcp`。

完整导入和 DCP 回读成功也不能代替 GETRF 的全量布线、实际 SLL 路径审计及 WNS 验证；这些不在本次执行范围内。
