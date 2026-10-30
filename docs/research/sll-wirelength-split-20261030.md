# E2 SLL 线长修订：显式入口连接

代码基线为 `8841dca23856cf7972fee955ca5c723d0a8af249`。在服务器独立副本 `experiments/preflight/20261030-sll-wirelength-split/repo`、分支 `codex/sll-wirelength-split-20261030` 实现；原 `sll-增量合并与加速` 分支保留整合基线。只修改基础线长拓扑及对应初始化，不重跑完整 GETRF/Vivado，不据此宣称 WNS 已改善。

## 单边界：修改前后

设发送端为 $s$，发送端所在 die 的接收端集合为 $R_0$，另一 die 的接收端集合为 $R_1$，SLL 入口、出口分别为 $v_{in}$、$v_{out}$。坐标为 AMF 布局坐标，不是 Vivado COLUMN。

\[
H_\alpha(P)=(\max_{p\in P}x_p-\min_{p\in P}x_p)+\alpha(\max_{p\in P}y_p-\min_{p\in P}y_p),\qquad
 d_\alpha(a,b)=|x_a-x_b|+\alpha|y_a-y_b|.
\]

空集/单点的 $H_\alpha$ 定义为 0；$w_n$ 是既有 net enhancement 权重，$\alpha$ 沿用 `y2xRatio`。

**修改前：**

\[
L_{old}=w_n\left[H_\alpha(\{s,v_{in}\}\cup R_0)+H_\alpha(\{v_{out}\}\cup R_1)\right].
\]

**本次实现：**

\[
\boxed{L_{new}=w_n\left[H_\alpha(\{s\}\cup R_0)+d_\alpha(s,v_{in})+H_\alpha(\{v_{out}\}\cup R_1)\right].}
\]

本地接收端继续共享超边；入口段单独计一次，默认不与本地接收端布线共享。远端接收端继续共享出口超边，没有改成逐接收端距离求和。不存在额外的入口系数 k/κ，也不按远端 fanout 倍乘入口段。入口到出口的专用 SLL 不按普通二维曼哈顿距离计入 L；其硬件延迟仍由既有工艺数据及路径时序模型处理。

入口/出口依旧是一对关联位置：QP 中共享同一个 crossing 变量，出口为入口变量加该次线性化采用的硬件映射偏移；投影到合法 tile 后更新映射。没有把两端当作彼此独立的可移动 cell。

## 多边界、双方向与可选分组

对每个 crossing $c$，令 $a_c=s$（若无父 crossing），否则 $a_c=v^{out}_{parent(c)}$。令 $R_c$ 为**终止在这个 landing 的真实接收端**，不包括还需继续跨界的接收端；$R_0$ 为不经过任何 crossing 的接收端。令 $C_j$ 为节点 j 的子 crossing。

旧模型把每个上游根、该处真实接收端和所有子 crossing 的入口合成一个超边：

\[
L_{old}=w_n\left[H_\alpha(\{s\}\cup R_0\cup\{v^{in}_c:c\in C_0\})+
\sum_c H_\alpha(\{v^{out}_c\}\cup R_c\cup\{v^{in}_d:d\in C_c\})\right].
\]

新模型为：

\[
L_{new}=w_n\left[H_\alpha(\{s\}\cup R_0)+
\sum_c d_\alpha(a_c,v^{in}_c)+
\sum_c H_\alpha(\{v^{out}_c\}\cup R_c)\right].
\]

因此连续跨界使用“上一出口到下一入口”的 die 内段，不能从原发送端跨多个 die 直接拉到每个入口。每条显式输入段按 crossing 计一次；默认 `SllPlanningAllowSplit=false` 不变。可选 split 打开时，每个独立 crossing 各有一段，仍不是按接收端复制。若某轮不再跨 die，现有 refresh/lifecycle 删除 crossing，该段随之从公式、QP 和新缓存中删除。

## 如何进入 QP

沿用当前 B2B 装配，没有增加另一轮求解。对维度 $d\in\{x,y\}$，$\alpha_x=1,\alpha_y=\alpha$；在当前线性化坐标 $z^0$ 上，超边 $H$ 的 B2B 二次近似（忽略常数）为：

\[
B_d(H)=\sum_{(i,j)\in E_d(H)}
\frac{g w_n\alpha_d}{(|H|-1)\max(0.5,|z^0_i-z^0_j|)}(z_i-z_j)^2.
\]

$E_d(H)$ 沿用“最小端—最大端，加每个内部端分别连最小/最大端”的边集；$g$ 为既有 `generalNetWeight`。$z_i$ 包含真实 pin offset 或虚拟出口 offset。上式对 0/1 个端子的项为 0。

单边界修改前为 $B_d(\{s,v_{in}\}\cup R_0)+B_d(\{v_{out}\}\cup R_1)$；修改后为：

\[
Q_{wire,new,d}=B_d(\{s\}\cup R_0)+
\underbrace{\frac{g w_n\alpha_d}{\max(0.5,|z_s^0-z_{in}^0|)}(z_s-z_{in})^2}_{\text{新增独立两端子边}}+
B_d(\{v_{out}\}\cup R_1).
\]

使用求解器形式 $\frac12 z^T Pz+q^Tz$ 时，这条边的矩阵权重是 $W=2gw_n\alpha_d/\max(0.5,|z_s^0-z_{in}^0|)$；两端可动时向两个对角加 W、两个交叉项加 −W，offset 进入 q。若真实源端被冻结，只移动虚拟端，固定端按既有规则折入 q。这是原有 B2B 归一化，并非新增缩放参数。

本次没有修改 SLL 数量项、动态 τ、密度项/锚点、时序牵引、位移限制和合法化。它们继续按原配置与 $Q_{wire}$ 联合求解。显式边保证线长目标看得到这段距离，但不保证最终 source/column 一定移动到特定位置；其它连网、区域约束、容量和时序仍共同作用。

## 初始化与缓存的一致性

`localTrees()` 前 $N_c+1$ 项仅包含接收端超边，后 $N_c$ 项为显式两端子输入段。`addWireModel()`、`wireCost()`、`wireLengthUncached()` 与 `beginWireLengthBatch()` 共用该构造，因此候选代价、缓存和矩阵不会分别使用不同的 L。

初始化将可选 tile t 的相关线长写为：

\[
J(t)=w_n\left[d_\alpha(a_c,t_{in})+H_\alpha(\{t_{out}\}\cup R_c)
+\sum_{d\in C_c}d_\alpha(t_{out},v^{in}_d)\right]+\lambda_\rho\Delta\Phi_t.
\]

其它不随 t 变化的超边是常数，不进入比较。$\Phi_t(u)=\max(0,u-\rho_*C_t)^2/C_t$，$\Delta\Phi_t=\Phi_t(u+1)-\Phi_t(u)$；容量参数、列门槛、源端 X 优先和 Y 同分规则沿用原实现。本次移除初始化接口中的 input bbox，直接接收 root，避免误把本地 receivers 放回入口成本。

第一次串行 seed 时子 crossing 尚未产生，先用入口段和已知 landing 接收端；拓扑构造完后，对新生/重新分配 crossing 在已选列内做一次 Y 修订，加入全部已知子 crossing 输入段。保留的合法 crossing 继续使用 QP 投影结果，不重新初始化。此过程是既有有界初始化策略，不宣称全局最优。初始化不调用时序评分。

## 验证

实现提交：`417484ecb714457d23b21ac43f34c115e08dd369`。新冻结构建：`build-20261030-200320-571931-417484ec`，标准入口 `amf3.py build --jobs 2 --profile --no-set-current` 通过，源码与构建快照逐文件 SHA-256 一致。正式基线分支及 current 构建指针未更新。

- 10 个模型/回归程序最终通过，覆盖源端与入口同在大包围盒内、远端重复 fanout、无本地接收端、跨多个边界、双方向、中间 die 无接收端、可选分组、容量、投影、出口时序修正和生命周期。
- 隔离模型样例：入口/出口 X 从 20 改到 70，源端 X=80，本地和远端超边范围均不变；新加权线长从 890 降到 765，差值恰为 `2.5 × 50`。增加 100 个重合远端接收端不改变线长。
- 真实 U250 资源、生产 `SllPlanner::addWireModel()` 集成通过：本地接收端 X=10、130，源 X=80，SLL 入口 X=33（物理 COLUMN 189），远端接收端 X=70。X/Y 两维均有负的真实源—虚拟入口交叉项；X 源端梯度约 +2，下降方向为向左靠近入口。
- 在这个生产线长矩阵上加入明确的测试锚点后，独立连续求解得到源端 `80 → 73.56168201`、入口 `33 → 65.34826262`。**这些是投影前坐标，不是运行完整 placer、10 单位限幅和合法列投影后的结果。** 该实验只证明独立连接确实影响求解，不预测最终列或 WNS。
- 缓存开启/关闭时，移动真实源端 5 单位得到相同的 5 单位线长改善；既有随机缓存测试的 80,000 次查询也通过。
- 原联合 QP 集成及非联合兼容集成通过，覆盖两轮求解、10 单位限幅后 11/12 列间距换列、动态变量增删、精确通道导出、现有 STA 与出口修正。联合 STA 检查了 3 条跨 die 连接，其中 2 条触发既有长出口段修正。
- 一处旧多边界初始化断言原本预期第一入口 Y=12；新模型将中间接收端分支和继续跨界分支分开后，正确最小值变为 Y=14，第二入口仍 Y=72。测试已按此几何变化更新，初次失败日志保留。

首次构建入口因机器配置仍指向旧副本而在配置/编译前拒绝执行；通过独立 `AMF_MACHINE_CONFIG` 绑定新副本后构建成功，没有修改历史机器配置。该入口失败和初次旧断言均保留记录。

没有新运行完整 GETRF 布局或 Vivado 布线，尚无最终 WNS、SLL 热点和全流程耗时结论。完整记录位于服务器本次 preflight 目录；汇总见 `docs/provenance/sll-wirelength-split-20261030.json`。
