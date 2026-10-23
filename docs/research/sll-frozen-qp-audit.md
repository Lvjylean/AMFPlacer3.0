# SLL分项梯度和冻结QP诊断

这组只读插桩保存指定QP的原目标、实际求解器目标、全体SLL模型和指定PU的分项梯度，供离线比较小tau的额外位移。没有改变SLL公式、系数、运行tau/gamma、原步长或接受策略，也没有加入压峰。

## 启用范围

默认关闭。只有同时提供 `SllQpAuditCellsFile` 和 `SllQpAuditQps` 才采集。cell列表用完整名称逐行指定，在每个指定QP重新解析为PU；因此不依赖历史PU编号，并涵盖目标PU内其他cell的全部关联net。`SllQpAuditQps`在JSON配置中必须为分号分隔的正整数QP编号，例如`53;54;55`（旧JSON解析器会错误拆分字符串内的逗号），不是全局布局轮号。需要既有`SllQuantityEnable`或`SllQuantityDiagnostics`开启，使原SLL诊断对象存在。

`configs/diagnostics/sll-frozen-qp-overlay.json`仅为诊断选项模板，不会被自动应用或启动实验。模板对应此前A_load_reg与din0_buf1_reg两个cell。现有运行参数继续为tau=10、gamma=0.02、SLL长度60tile、y2xRatio=0.4、c约24；归档保留实际浮点系数，离线工具默认用精确24并明确输出两者。

## 分项口径

`components.tsv`记录目标PU在同一展开点的X/Y分量：

| 字段 | 含义 |
|---|---|
| wire_b2b_unenhanced | 去掉已有net/pin增强后的B2B项，仍保留原fanout系数、距离分母、宏pin偏移和y2xRatio；不是直接HPWL导数 |
| inherited_cluster_net_enhancement | 加入已有整net簇权重产生的差值 |
| inherited_timing_net_enhancement | 再加入已有整net时序权重产生的差值 |
| inherited_pin_pair_enhancement_and_assembly_roundoff | 再加入已有pin-pair权重，以及局部重算与实际浮点累加顺序的残差；不将未知来源的pair权重一概称为时序 |
| timing_slack_and_pairing | 本次构造的slack边增强和LUT配对项 |
| anchor_timing_target | 时序优化额外加入的定点锚定，单独从上一项中扣除 |
| anchor_legal_site | 宏合法site锚定 |
| anchor_clusters | 用户/FF簇锚定 |
| anchor_self_position | 锚在当前位置的伪网，理论展开点梯度为零但保留矩阵刚度；浮点残差按实际记录 |
| anchor_region | 区域偏好锚定 |
| original_total | SLL注入前原QP梯度 |
| sum_residual | 上述原项求和减去original_total，超过容差则快照标记未完成 |
| sll_all_nets | 所有适用net汇总到PU后的c∇F，与原项分开 |

乘法增强按“未增强→簇增强→整net时序增强→pin-pair增强”固定顺序分解，交叉效应归到后加入的项。分解用于解释现有目标，不改变运行权重。私有重算缓冲区复用生产B2B代码并匹配其逐轴跳过规则，不重写现场net权重。

## 快照内容

每个`frozen-qp/qp-N/`为独立快照，包含：

- `original-y.bin`：完整Y方向原始triplet/对角项/b、当前XY、XY warm start、SLL可移动掩码、全部PU的原SLL梯度、目标PU索引、实际系数、原求解容差、迭代上限和位移限制。重复triplet原样保存。
- `effective-y.bin`：从真实求解器的只读观察器获取，位于原有稳定性保护之后、CG之前的实际A和b。其b包含本次实际SLL注入；离线工具扣除已记录的注入，得到同一保护锚点下的原b。
- `model.bin`：全体SLL模型的net、所有pin、PU映射、宏/pin Y偏移、net权重、优化资格、物理边界、原tau/gamma和有效Y域。不是只导出两个PU的邻网，因此能计算改变全体net的tau后对这两个PU的耦合影响。
- `target-members.tsv`、`incident-nets.tsv`、`incident-pins.tsv`：目标PU成员和全部关联net清单。参与SLL模型的net保留所有pin。power/global clock等排除项仍列出完整度数、排除原因及目标成员pin，不复制其无关的全器件pin；`pin_scope`明确标记此区别。
- `raw-xy.bin`、`written-xy.bin`：原求解结果及原写回后的XY，用于区分求解位移与裁剪。只读导出，不执行新的写回。
- `metadata.json`、`COMPLETE.json`：QP、阶段、轮次、数值保护状态和完成标记。根目录`configuration.json`保留配置及实际可执行文件路径，结合运行manifest确认源码和二进制来源。

只保存完整Y矩阵，因为SLL仅注入Y，原X/Y QP分别求解。X坐标、warm start及原始/写回结果仍保留，以解释原二维位移限制。这里没有声称重放后续区域裁剪、合法化、打包或完整布局轨迹。

## 存储和失败处理

`SllQpAuditMaxBytes`默认2147483648，即全部选定快照payload合计2GiB，小型根目录配置与索引另计。每次写入前检查剩余预算，禁止覆盖已有快照。预算不足或I/O失败时保留不完整现场并写入`index.tsv`，不改变QP输入、求解结果或触发求解器回退；不写`COMPLETE.json`。离线工具拒绝缺少完成标记、截断、非法维度或梯度无法复算的快照。默认关闭时不创建冻结快照目录。

格式采用明确的little-endian IEEE754字段，不保存C++结构体padding。归档和离线读取的实现共用`FrozenQpArchive.h`。诊断额外耗时写入索引，不能把启用完整快照后的墙钟当作原算法性能。

## 离线比较

未来有完整快照后，在服务器执行：

```bash
builds/<已验证构建>/build/replayFrozenSllQp \
  <SllReportDirectory>/frozen-qp/qp-55 \
  <新的输出文件>.json 24
```

这是冻结系统的离线线性代数和SLL函数计算，不调用AMF布局入口，不修改任何布局或配置。工具复算原tau的全体PU梯度并核对归档，然后比较原tau、0.2、0.1。保持同一有效矩阵和同一原b：

\[
\hat y_0=-A_{\mathrm{eff}}^{-1}b_{0,\mathrm{eff}},\qquad
\delta y_\tau=-A_{\mathrm{eff}}^{-1}\bigl(24\nabla F_\tau(y_k)\bigr).
\]

报告全部系统的额外位移范数/最大值、两个PU相对SLL关闭和原tau的额外Y位移、原梯度、SLL总梯度、实际对角刚度和既有数值保护的贡献，并列出目标PU每条关联SLL net的贡献。还计算未裁剪候选的原ΔQ、24ΔF和ΔJ。

离线CG默认容差1e-10、最多10000次，用于测量冻结系统响应，不修改AMF原容差。必须通过实际残差检查，否则拒绝给出成功位移结论。结果是未裁剪系统的静态敏感性，不等同原容差下的实际迭代结果或完整布局的新轨迹；候选越界net数单独输出。

历史运行没有保存完整冻结矩阵，新增功能不能补回缺失的旧快照。完成编译与合成测试不代表已经获得GETRF在新tau下的完整位移数据；此功能留给后续获得授权的采集使用。
