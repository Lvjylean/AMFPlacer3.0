# 按器件选择 SA 第三列惩罚

本副本基于已发布 `b0c4ad1e4838d662c2ff9c49c93570586eca7608`，位于 `experiments/preflight/20261102-sa-device-policy/repo`，分支 `codex/sa-device-policy-20261102`。修改前完整已跟踪源码归档位于副本父目录 `baseline-source.tar.gz`，哈希和来源见同目录 `preparation.json`。原发布副本、历史实验和配置不修改。

用户本轮仅授权按 `device` 区分当前 SA 策略，去掉 U250 的第三列水平距离附加罚。普通全量路径通过 `DeviceInfo::getPlacementPolicy()` 选择：

| device | SA 的 gridX=2 水平距离附加罚 |
|---|---|
| U250 | 关闭 |
| VU095 / VCU108 | 保留原公式和触发条件 |
| 未指定 | 保留已有 VCU108 默认值 |

没有新增配置键或修改历史 JSON。现有未知器件/预留 VHK158 的拒绝规则保持。`ProLegacyIOBoundary`、`ClockRegionColumnStretch` 和其他聚类开关不控制这一项。

仅完整 SA 评价中已有的附加项受器件选择控制：当簇对至少一端位于第三列时，原公式增加 `0.5 * regionW * abs(gxA-gxB)`。正常的加权水平/垂直距离、固定端点、重叠罚等保持。VU095 的贪心评价原本没有此附加项，本轮仍保持原行为；U250 的完整评价现在也没有此项。DFX 的既有 physical-grid 评价没有该项，本轮不改其行为。

二进制 capability 新增 `sa_device_policy_schema=1`；普通 SA 启动日志显示实际 device 和惩罚 enabled/disabled，便于后续实验核对。

本轮不修改 SA 多启动、容量模型、跨 SLR 目标、Y/X 比例、SLL 延后规则、扩散限制或可视化。不自动运行完整布局/布线，不推送 GitHub。验证使用完整 AMF 构建、既有器件策略矩阵和新的 SA 数值/兼容测试；结果另保存在副本父目录 `validation/`。
