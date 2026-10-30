# SLL 线长建模已知问题：入口侧共享超边掩盖必经连接

状态：**已确认，尚未修复**。本次整合保存当前实现，不修改 L、QP、时序或容量算法。后续另做模型修订与对照实验。

## 当前实现

对跨一个 die 边界的 net，令 s 为发送端，R_local 为本地接收端，R_remote 为远端接收端，v_in / v_out 为关联的 SLL 入口、出口虚拟位置。

当前 `SllVirtualModel.h::localTrees()` 构造：

```text
H_in  = {s, v_in} ∪ R_local
H_out = {v_out} ∪ R_remote
L_current = w_net × [HPWL_alpha(H_in) + HPWL_alpha(H_out)]
```

`SllPlanner.cc::addWireModel()` 分别对两个超边做 B2B 二次近似。发送端与入口都处于包围盒内部时，两者之间不一定存在直接的基础线长边；发送端到入口的距离变化可能不改变超边 HPWL。虚拟 cell 已参与 QP，问题是连接模型没有独立保留这一必经段。

`SllSourceNearInitialization` 仅在初始化/重新分配 crossing 时优先靠近发送端。容量投影依据 QP 坐标和资源可行性，不持续替代缺失的两点线长项。额外时序牵引按预测延迟与连接预算有条件启用，也不等价于持续存在的基础线长边。

## 已确认的诊断

GETRF/U250 10 ns 固定 SLL 实验中，源 `bitcast_ln167_reg_4868_reg[60]/Q` 对应的 net 有 145 个本地与 111 个远端接收端。源/关键直接接收端的物理 COLUMN 为 422/575，规划与实际 crossing 均为 189。

固定最终真实 cell 坐标和其他 net 占用，使用该实验实际冻结构建的模型复核：

| 位置 | 归一化两侧 HPWL | 换入后整列占用 | tile 占用 |
| --- | ---: | ---: | ---: |
| 原 COLUMN 189 | 391.3 | 38.89% | 18/24 |
| COLUMN 438，保持入口/出口 Y | 391.3 | 69.10% | 7/24 |

这里 HPWL 使用 AMF 坐标和 alpha=0.4；COLUMN 是 Vivado 物理列，不能混用两种坐标。此复核说明最终近源列有容量，且现有线长项看不到关键分支缩短的收益；不是一次新的布线实验。

原始逐轮 AMF 轨迹已在此前清理中移除，无法证明首次到达 189 的具体轮次或当时容量情况。历史构建 `build-20261028-173650-718371-64a4660c` 的导出只在既定 tile 内分配通道，不能将后来加入的最终容量重投影当成此次 189 的原因。

服务器证据：`experiments/evidence/20261030-getrf-sll189-trace/analysis.json`；实际固定 DCP 属于 `getrf-u250-e2-native-carry-source-10ns-route-20261028-193812-248923`。历史延迟误差不代表后续出口侧校准模型的误差。

## 后续已讨论的修订方向——本提交未实现

```text
L_next = w_net × [
    HPWL_alpha({s} ∪ R_local)
    + d_alpha(s, v_in)
    + HPWL_alpha({v_out} ∪ R_remote)
]
d_alpha(a,b) = abs(x_a-x_b) + alpha × abs(y_a-y_b)
```

建模假设：本地连接与发送端到 SLL 入口的普通布线默认不共享，不预先扣除可能的共享部分；真实共享由路由器决定。远端仍采用共享超边，同一 crossing 的发送端到入口只计一次，不按远端 fanout 复制。入口与出口继续按通道几何映射关联。

后续修订应一致覆盖超边构建、QP、线长缓存与初始化成本，并验证本地接收端为空、同时有本地/远端接收端以及多边界情形。修改前，本分支只可称为当前功能整合基线，不能称为已修复 SLL 建模的版本。
