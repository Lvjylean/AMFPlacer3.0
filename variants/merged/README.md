# SLR / HPIO 合并阶段源码变体

默认主源码仍为独立两轮 SLR。本目录的 src/ 保存相对主源码不同的完整文件，其他源码共用；不是另一个可直接构建的不完整项目。

按 [版本说明](../../docs/research/amf3pro-srl-compatible-20261012.md) 在独立工作树覆盖后构建，并运行 scripts/verify_srl_release.py --variant merged 验证。仅更换 JSON 不会改变阶段流程。

configs/experiments/ 提供弱牵引开／关两份配置。保留先 SLR X 腾空间和裁侧、再 HPIO Y 腾空间和裁侧、最后合并 GP 的顺序。预算沿用 HPIO，不增加 SLR 独立轮数。算法来源为 447b35b937f03529696b734abefd8ff453162019。
