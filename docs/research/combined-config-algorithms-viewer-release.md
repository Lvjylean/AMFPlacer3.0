# 072 配置、算法与多器件查看器整合

此新分支在纯查看器提交 `e222992ae0cfc870322d6c590db8dc4b846a53c4` 上整合 072 的当前代码。
纯查看器分支 `codex/placement-viewer-multidevice-20261021` 保持独立。

## 来源与合并范围

| 来源 | 保留内容 |
|---|---|
| 072 device-placement-policy-83 工作树 | VU095/U250 器件策略、可配置旧纵向拉伸、器件能力检查、16 项配置与调度 |
| 072 vhk158-resource-spread-fix 工作树 | 74q 后续源码、原生静态缓存及局部引脚访问过滤、LUT 引脚映射/LOCK_PINS、clock guidance 同会话处理、VHK158 稀疏资源模型和修补、可视化输出钩子 |
| 纯查看器已发布提交 | U250/VHK158 查看器、器件分类入口、历史版本兼容、真实专用资源显示及预览进度 |

两个算法来源均含未提交覆盖层；来源提交号不能代替工作树哈希。逐文件来源及合并后哈希保存在同目录 provenance 清单中。
共同代码保留 `slr-hierarchical` / `cr-slr-hierarchical`、`independent-max2` / `merged`、Pblock 独立 partition/floorplan 与硬约束、宏合法化、legacy_components。
旧 paper-hierarchical 仅兼容别名，region-gain 仅显式遗留入口；未作为默认恢复。

合并仅增加共同能力声明、combined 配置 profile，并用原有 VHK pilot 条件接通器件策略；未改 SA 公式、距离系数、线程数、阶段预算或历史结果。
VHK158 不沿用 VU095 的旧牵引/拉伸，仍仅支持已验证的单时钟 LUT/FF pilot。
VU095 恢复的旧 Y 拉伸可配置；U250 默认关闭，保持当前行为。

## 对 AMF 主代码的影响

查看器本身读取文件，不调用布局求解或改动 LOC/BEL。轨迹输出钩子只读坐标，开启后增加文件 I/O 和时间，写失败仍会报错；不能把有无轨迹的墙钟当成无开销等价。
本次完整分支包含用户明确要求的 AMF 算法、器件和 DFX 更新，因此相对 59 版有主源码变化；“纯查看器分支不改 src”不适用于这个完整分支。
不上传 DCP、bitstream、轨迹、预览/GIF 缓存、实验日志或编译二进制。既有大型输入仅引用路径和哈希。

## 验证界限

发布检查包括独立编译、器件门控/聚类配置/增量配置与局部引脚访问回归、查看器回归。
没有重新运行全量或增量布局布线，历史 74q、U250 和 VHK pilot 结果不冒称本次合并二进制的新验收。
实际命令、结果与二进制 SHA 见 `docs/provenance/combined-release-validation-20261021.json`。
