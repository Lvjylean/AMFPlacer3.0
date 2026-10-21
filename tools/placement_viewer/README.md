# AMF3 多器件布局查看器

独立的 Python/HTML/Canvas 工具，支持 U250 和 VHK158；先选择器件，再选择该器件的历史运行，查看关联 DCP 信息和 AMF 布局轨迹。

## 功能

- U250：兼容已有 site 导出、physical boundary 元数据和规则 CR 网格。
- VHK158：读取与实际 part 和 device.zip SHA-256 绑定的稀疏 CR/SLR 几何，不填充虚构 CR。
- 专用资源层：PS、NoC、CPM5、GTYP 使用已导出的真实 site/tile 数据和不同颜色。随代码提供的 inventory 仅覆盖已查询的左侧两个非 SLICE 区域；点是实际 tile 锚点，色块/标签不是硬 IP 的硅面积，也不推测 NoC 连线。数据哈希不匹配时不套用。
- 兼容 `placement/visualization/` 和旧 `placement/` 两种轨迹目录；缩放至实际 cells、阶段播放、模块追踪、二进制帧、可选预览及 GIF 导出。
- 预览明确区分未生成、生成中、失败、完成，显示真实帧数；不会把“尚未生成”称为后台生成中。新显示样式使用独立预览缓存名，不覆盖旧缓存。
- DCP 分类只用 ZIP 内 `dcp.xml` 的 part/top 信息并核对 config；不调用 Vivado，不下载 DCP。配置冲突和未知器件明确列出；只有 DCP、没有轨迹的记录可查路径但不能播放。
- 每次打开都核对服务返回的完整轨迹目录，避免复用端口后打开了错误的历史设计。

## 单个运行（服务器）

依赖 Python 3.9+；纯浏览使用标准库。预览/GIF 需要 Pillow 和 `DejaVuSans.ttf` 字体（Linux 通常由 `fonts-dejavu-core` 提供）。

```bash
python3 tools/placement_viewer/placement_viewer.py \
  --config /absolute/run/config.json \
  --traces /absolute/run/placement/visualization --port 8765
```

旧运行将 `--traces` 指向 `/absolute/run/placement`。输入 config 中网表/device ZIP 和物理模型路径必须在服务主机上可读；不修改历史 config。VHK158 另需 `visualization device metadata file`，其 schema、part 和源设备哈希会校验。`device` 必须为 `U250` 或 `VHK158`。所有服务仅监听 `127.0.0.1`。

## 器件 → 历史设计入口（macOS/Linux 客户端）

1. 将整个 `tools/placement_viewer` 目录放到服务器和客户端。无需编译 AMF。
2. 在客户端复制 `launcher/viewer_settings.example.json` 为 `launcher/viewer_settings.json`，填写 SSH 别名、远程项目根目录及工具目录；支持已有 `~/.ssh/config`。私钥可用 `ssh_key` 或 `AMF_SSH_KEY` 指定，不将密钥或个人设置提交 Git。
3. 执行：

```bash
python3 tools/placement_viewer/launcher/open-amf-viewer-mac.py
```

macOS 也可运行同目录 `.command`。脚本名保留历史兼容，Linux 使用相同 SSH 入口。浏览器默认打开 `http://127.0.0.1:18760/`；端口可由 `picker_port` 修改。初始化只扫描目录；选中运行后才连接其查看器。不会启动 AMF/Vivado 实验。

目录扫描范围是配置根目录下的 `experiments/**/runs/*`。保留 `config.json` 的 DCP-only 记录也会展示。额外服务器 DCP 可通过服务端 `launcher/artifact_links.json` 登记（格式见测试 `test_catalog.py`）；登记方须核实来源，工具不会把他机路径假装成本地验证。该文件、实际运行索引和连接令牌均忽略，不随发布提供。另提供手动白名单入口 `multi_device_viewer.py --catalog <catalog.json> --port 8772`，格式为 schema=1，runs 内含 id/device/config/traces/label/scope。

## 可选派生文件

```bash
python3 -m pip install -r tools/placement_viewer/requirements.txt
python3 tools/placement_viewer/prepare_preview.py --run /absolute/run --workers 4
python3 tools/placement_viewer/prepare_binary_cache.py \
  --config /absolute/run/config.json --traces /absolute/run/placement/visualization
```

预览生成单次最多四个 worker、有目录锁和进度文件。查看器本身不自动生成缺失预览；以上命令是显式生成入口。GIF 导出由页面操作触发。派生文件写入 `.viewer-*` 或 `viewer-exports`，不覆盖输入轨迹、DCP 或布局结果。

`build_inventory.py --geometry <device_geometry.json> --exports <directory> --output <inventory.json>` 从真实导出生成专用资源层。输入为 upper/lower-tiles.tsv（tile/type/row/column）及 upper/lower-sites.tsv（site/type/tile/cr）；不执行 Vivado 查询、不包含布局算法。随代码的 inventory 保留原始导出哈希用于追溯。

## 与 AMF 主程序的关系

此发布从已上传的 `incremental_compilation_静态布线bug` 提交 `39464f8f` 派生，仅新增工具和说明；不修改 `src/`、CMake、默认 placer 参数、AMF 调度或 Vivado 流程。最新 VHK158 placer 适配与未上传的其他算法修补不包含在此次工具提交中。

查看器消费已有 `DumpAllCoordTrace-*.gz`，不向 placer 回写坐标或约束；没有轨迹的旧 DCP 无法仅靠这个工具重建布局迭代。当前实验构建已有的 `visualization` 轨迹导出属于生产端：开启导出会增加文件 I/O/时间/磁盘占用；本次没有新增或修改该 C++ 导出逻辑。浏览、生成缓存和 GIF 也会消耗 CPU/内存，但不改变布局求解结果。

发布不包含 DCP、bitstream、轨迹、预览/二进制缓存、个人运行索引或构建结果。历史原件、服务端口和现有实验不由此发布更新。

## 回归验证

```bash
python3 -m unittest discover -s tools/placement_viewer -p 'test_*.py' -v
```

测试覆盖设备/几何不匹配、历史目录、DCP 分类冲突、回收端口设计身份、预览状态与追踪读取；测试产生的合成轨迹只存在于临时目录。
