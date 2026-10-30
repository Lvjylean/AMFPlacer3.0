# 布局紧凑回放

部署验证及限制见 `docs/research/placement-playback-compact-20261022.md`，后续 Mac 本地缓存修订见 `docs/research/placement-playback-local-cache-20261022.md`。生成器运行于 099 Linux，依赖 Python 3 和已有 Pillow；Mac 使用标准库 `local_playback.py` 提供页面及已缓存图片。

- `placement_viewer.py --traces <原轨迹目录> --config <原配置> --port <端口> --preview-cache <独立缓存目录> --preview-limit-mib 32`。
- 现有服务迁移可先用 `metadata_snapshot.capture` 从同一 run 的旧 loopback 服务生成 `<缓存目录>/metadata.json.gz`，再传 `--metadata-cache`；校验配置 SHA-256、输入路径/大小/mtime、源 run 和资源类型计数。输入变化时拒绝复用。
- 启动主服务可用 `nice -n 15 ionice -c 2 -n 7 taskset -c 23`；后台渲染线程自行降为 nice 19 / idle I/O。不要将主服务设成 idle，否则显式完整坐标查看也可能长时间被磁盘负载延后。
- 两组缓存置于同一父目录，使用共享 `render.lock` 串行生成。缓存含元数据，容量按已分配磁盘块计算；满额按 LRU 淘汰图片，绝不删除原 gzip 轨迹。
- 默认快速预览；完整坐标模式延迟加载名称字典，可在配置面板搜索 cell 并查看当前帧的 X/Y。未就绪帧立即暂停并保留上一个画面及其阶段标签，不拿旧帧冒充新帧。
- 缓存生成属于回放服务后台任务，非布局实验；资源紧张自动暂停，已缓存图片仍可读取。缓存文件需与渲染版本、配置和源轨迹匹配。

Mac 启动：`python3 -B scripts/diagnostics/placement_playback/local_playback.py --config <配置.json>`。配置的 `viewers` 数组逐项包含 `name`、`port`、`upstream`、`host`、`remote_cache`、`cache` 和 `limit_bytes`。上游 SSH 转发端口必须与本机服务端口不同。当前本地 18823/18824 对应私有转发 18833/18834；后者对应远端 45455/45454。

当前每组远端 28 MiB + Mac 4 MiB，总限额仍 64 MiB。Mac 后台约每 30 秒同步已生成图片，按需请求唤醒；首帧保留，其余图片 LRU 淘汰。完整坐标 GET/POST 通过上游转发并保留 gzip，默认不读取完整名称字典和坐标。`--seed-only` 仅用于切换前预热，历史 `seed-config.json` 指向切换前端口，不可在切换后再次使用；使用当前 `config.json`。

测试：`python3 -B -m unittest discover -s scripts/diagnostics/placement_playback -p 'test_*.py' -v`。本地代理集成测试会短暂监听随机 loopback 端口，结束即关闭。

## 本地 SLL / Pblock / 独立增量布局扩展（2026-10-30）

继续使用原 Mac 启动器 `experiments/preflight/20261020-viewer-launcher-compat/mac/open-amf-viewer-mac.command`，打开 `http://127.0.0.1:18760/`，选择 U250。新增三条本地记录：

| 记录名称关键字 | 显示内容 |
| --- | --- |
| `12 ns · E2 全量 + Pblock + SLL` | 855,092 个真实 cell；24,795 条 net 的 25,359 个 crossing；1,727 对占用 tile；两个 Pblock |
| `12 ns · 单独增量模块 A` | 2,956 个活动 cell，另可显示 2,681 个静态接口上下文 cell |
| `12 ns · 单独增量模块 B` | 2,956 个活动 cell，另可显示 2,678 个静态接口上下文 cell |

这些记录由已有 AMF 最终 `placement.tsv`、器件 site 坐标、区域文件与 `sll-plan.tcl` 转换。坐标和图层已保存在本机，打开它们不需要 SSH。数据目录为 `experiments/preflight/20261030-local-viewer-layers/`，DCP 未下载。旧历史记录仍按原播放方式打开。

- **SLL tile cells**：入口、出口使用通道表中各自的 AMF 坐标，保留方向和 X 偏移，不假定两端同 X。多个 crossing 重叠时显示 tile 占用；点击 tile 或搜索 net/resource 查看成对虚拟点、物理 COLUMN、精确通道及相关 net。入口为青色方块，出口为粉色菱形。连线仅表示 SLL 配对，不是普通布线路径。
- **容量**：按 canonical resource 去重，两个方向与两个端点不重复增加供给或需求。当前器件每个 tile 对为 24 条通道。色阶是 tile 级别的 ≤80%、80–100%、≥100%；不改变 placer 阈值，也不是整列利用率。显示全部计划的占用，搜索不会改变容量统计。
- **Pblock**：按原 `resource_regions.tsv` 中 CR 范围绘制，不把多个离散范围合并成一个大矩形；CR 分界由器件 site 坐标映射得到。不是 routing footprint 或可用 site 的精确掩码。cell 归属按真实层级前缀匹配，不能用当前坐标是否位于框内替代归属。
- **单独增量**：默认只显示活动模块，支持 Pblock 选择与聚焦。开启“显示静态上下文”后，缓存中的相关接口单元以淡色显示；这不是完整静态 shell。两份真实增量记录均为 **0 个模块规划 crossing**，不代表整个设计没有静态 SLL。
- **数据阶段**：以上三份均为 AMF 最终布局快照，先于 Vivado 兼容放置/布线。SLL 是规划值，未用实际 route 审计覆盖；历史 DFX 接口计划偏差依然存在。原实验没有逐轮完整坐标/SLL 导出，不能恢复 QP 过程。

### 文件与后续导入

`local_layout.py` 提供本地只读服务，复用现有回放 UI 的布局、缩放、资源筛选、搜索和页面租约。`layered_view.html` / `layout_layers.js` 增加独立图层；当前本地 bundle 视图不提供 GIF 或旧版任意分组追踪，历史播放器功能保持原样。

在报告所在服务器，用标准库转换器读取已存在的结果：

```sh
python3 build_layout_bundle.py --run /path/to/amf-run \
  --full /path/to/full-amf-run --placement /path/to/placement.tsv \
  --out /path/to/new-local-layout-bundle --title '设计名称'
```

`--full` 提供 Pblock 定义；全量运行时可与 `--run` 相同。增量运行从 `native/cells.tsv` 和 `native/static_placement.tsv` 读取活动单元/上下文。`--out` 必须是新目录。仅传输生成的 bundle，不传 DCP；将 `local_layout_bundle` 加到本地已注册记录中即可复用历史入口。也可直接启动：

```sh
python3 local_layout.py --ensure --bundle /absolute/path/to/bundle
```

bundle 的 `manifest.json` 使用 `amf-local-layout-v1`，记录来源路径、输入哈希、器件、Pblock 和 `frames`。每帧分别引用：

```json
{"name":"qp-12","label":"QP 12 合法化后","binary":"qp-12.bin.gz",
 "dictionary":"qp-12.dictionary.json.gz","layers":"qp-12.layers.json.gz"}
```

binary 沿用 `<uint32 id,float32 x,float32 y>`。dictionary 按 ID 存 `names/types/regions/roles`，role 为 `active` 或 `context`。layers 存 `state/crossings/tiles`；crossing 包括 net、入口/出口坐标、方向、resource 和 tile ID。每帧拥有独立图层：crossing 消失必须输出空数组；未记录时省略 `layers`，界面显示缺失，不回填上一帧或最终计划。未来逐轮数据可按此格式接入；本次没有修改 placer 的导出代码。
