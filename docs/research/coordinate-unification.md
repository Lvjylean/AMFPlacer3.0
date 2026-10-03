# xcvu095 与 U250 坐标规则与实现

2026-09-29。统一的是坐标构造约定：**器件自身的 tile 列基准＋列内位置＋独立特殊资源列**。U250 不复制 xcvu095 的固定列号，不匹配其总宽度或 CR 宽度。VCU108 的旧器件是 xcvu095，并非 Zynq 的 zu095。

## 当前实现与此前误差

正式转换入口为 `scripts/prepare_fabric_device.py`，默认模型为 `tile-columns-subsites-v3`；X 映射实现位于 `scripts/fabric_x_coordinates.py`。新器件在服务器 `data/devices/u250-tile-columns-v3/`，配套边界在其 `model/` 下。独立使用配置为 `configs/experiments/getrf-u250-tile-columns.json`。

此前 `normalized-rpm-x-tile-row-anchors-v2` 的 X=(RPM_X−48)/16 仍是 RPM 归一化，**没有实现用户要求的 095 式列构造统一**；它与更早的 U250 坐标数值相同，也是上轮 placement 未变的输入层原因。现在该模型仅保留 `--x-model rpm` 显式复现入口，旧输入、实验和校准产物保留。

更早的 `u250-legacy-shared-scale-v1-experimental` 候选误将统一理解为直接 X×0.5 并套用095的特殊Y偏移，已停用。本次不是重新启用该候选。

## 从 U250 数据恢复列结构

数据来自 Vivado 2024.2 的 `xcu250-figd2104-2L-e` 全器件导出。三个输入各司其职：

- `sites.tsv`：237632 个 fabric site、所属 tile、RPM、CR、SLR、BEL、禁用属性。
- `structure_sites.tsv`：完整器件的 site，识别 CMAC、HPIO、PCIe、配置资源的位置。
- `structure_tiles.tsv`：用真实物理 COLUMN 区分同一普通列间隙内的独立特殊列。

普通 fabric 从 tile 名称中提取 tX，连续覆盖 0–147，共148个基准列。这里的 tX **不是**物理导出中的 COLUMN：COLUMN只用于识别和核对特殊列，不直接作为布局距离。

同一 tX 内按 RPM_X 排序恢复左右两个位置，左侧 −0.25，右侧 +0.25；只有一个位置的列，依据同一 tile 类型在已观测双位置列中的侧别。RPM 在这里仅用于顺序/归属核验，不用于统一线性缩放。

|U250 tile 类型|列内偏移 s|
|---|---:|
|CLEM、CLEM_R、CLEL_L、BRAM|−0.25|
|CLEL_R、DSP、URAM_URAM_FT、URAM_URAM_DELAY_FT|+0.25|

尤其 `CLEM_R` 的真实观测位置为左侧，不能凭名字中的 `_R` 判定右侧。BRAM18/BRAM36 是同一位置的容量视图，不重复占 X 列宽。

## 特殊资源位置与最终 X

对没有被普通 tX 骨架表示、且有实际 site 的已识别特殊物理 COLUMN，按每个独立 COLUMN 补一个单位列。一个 COLUMN 中的多个 site 不重复加宽；PCIe 与配置资源虽然在同一间隙，却占两个不同物理 COLUMN。

|特殊资源|物理 COLUMN|RPM_X|位于普通 tX 之间|补列基准|列内偏移|最终 X|site数|
|---|---:|---:|---|---:|---:|---:|---:|
|CMAC|86|312|7 / 8|8|0|8|12|
|HPIO_L|369|2576|73 / 74|75|−0.25|74.75|832|
|PCIe|697|4832|139 / 140|142|0|142|4|
|配置资源|698|4840|139 / 140|143|0|143|4|

完整硬块的坐标取补列基准；明确标为左半列的 HPIO_L 使用 −0.25。**“每个独立特殊列占1单位”是本次选择的单位列坐标约定，不是测量得到的硅片宽度，也不等于该资源形成全高度路由阻塞。** 插入条件来自当前器件数据，转换器没有写死 8/74/140；下式是对实际导出结果的归纳：

```text
B(tX) = tX + I(tX >= 8) + I(tX >= 74) + 2*I(tX >= 140)
X(site) = B(tX) + s(site)
```

这与095的“列基准＋列内偏移＋特殊列插入”表达方式一致，具体列数和间隙取自U250。例如：

|site所在位置|tX|新 X|
|---|---:|---:|
|起始 CLEL_R|0|0.25|
|CLEM / DSP|1|0.75 / 1.25|
|CMAC左侧 CLEM|7|6.75|
|CMAC右侧 CLEL_R|8|9.25|
|HPIO左侧 CLEM / CLEL_R|73|73.75 / 74.25|
|HPIO右侧 CLEL_R|74|76.25|
|PCIe/CFG左侧 CLEM|139|140.75|
|PCIe/CFG右侧 CLEL_R|140|144.25|
|最右 CLEM_R|147|150.75|

双位置普通列的间距为0.5，跨特殊列按插入结构增加间隔；这不是旧 RPM 坐标乘一个常数。全部237632个fabric site的X均发生变化。

## 器件和 CR 的实际跨度

fabric site锚点的X范围从旧模型0–316变为0.25–150.75，跨度150.5。这里报的是资源锚点的最大值减最小值，不是硅片轮廓宽度。095旧fabric的对应范围是0.25–85.75，跨度85.5；二者不需要相等。

|U250 CR列|fabric X范围|锚点跨度|
|---|---|---:|
|X0|0.25–20.25|20|
|X1|20.75–37.25|16.5|
|X2|37.75–60.25|22.5|
|X3|60.75–74.25|13.5|
|X4|76.25–94.25|18|
|X5|94.75–113.25|18.5|
|X6|113.75–132.25|18.5|
|X7|132.75–150.75|18|

这八组范围在16个CR行上一致。CR的坐标跨度自然不相同；没有按CR分别归一化或缩放。

## Y、边界与下游数据的一致性

本次针对X构造修订。Y继续使用上一版实际tile行锚点，不复制095历史图示矩形的中心偏移：

`Y = (tile_Y − y0) / pY + i * h / n`。

当前U250的y0=0、pY=1。SLICE的h/n=1/1；DSP与RAMB18为5/2；RAMB36为5/1；URAM为15/4。i是同一资源tile内按site_Y排序的序号。每CR覆盖60个SLICE行，SLICE锚点0–59，CR行节距60。整个Y范围0–959。这是下端资源行锚点语义，与095某些硬资源的历史图示中心偏移不同，不能声称旧Y常数已被原样统一。

物理边界生成器复用同一个新X映射。fabric与已识别特殊资源的X均使用准确锚点；其余非fabric资源只在需要时做RPM分段插值/端点线性外推，不能解释为已验证的具体子位置。

HPIO带由旧X=158改为74.75；SLR接缝仍为Y=239.5/479.5/719.5。边界惩罚保持1.5ns/SLR与0.5ns/HPIO。CMAC/PCIe/CFG仍只是局部IP候选，不自动收费，也不将其包围盒视为必经障碍。坐标更改与新增边界惩罚是两件事。

## 验证、使用与限制

证据目录：服务器 `experiments/preflight/20260929-u250-tile-columns-v3/`；本地轻量证据 `experiments/evidence/20260929-u250-tile-columns-v3/`。

- 全部237632站点按列基准＋侧别独立复算，最大误差0。
- 导出文本删除X字段后与v2逐行完全相同：Y、tile、CR、SLR、BEL与禁用属性均保持。
- 重新生成物理模型，并验证原始器件/坐标/规则/模型的哈希绑定。
- 36项Python回归通过，覆盖非均匀RPM、特殊列、多资源同列、误导性的_R名称、缺失结构拒绝处理、全器件源冲突、边界映射与历史输入兼容。
- 原生输入检查退出0，进程墙钟33.83秒；硬资源合法化退出0，进程墙钟85.59秒。分配26096个资源单元，12183对Carry/DSP专用级联保持同SLR、同列且相邻；占用唯一、禁用站点与BRAM18/36重叠槽检查通过。详见 `validation-manifest.json`。这些是进程墙钟，不是纯布局算法计时。使用冻结构建 `build-20260928-002048-349585-d534237b`，完整CLB布局与Vivado未运行。

新配置保留10ns、共享与SA的y2xRatio=0.71、BoundaryAwareClustering=false，作为输入/合法化验证条件；**0.71在新X尺度下未经校准**。前一轮三段延迟候选同样绑定旧RPM坐标，不能直接迁移。X变化会影响dx、近/中/远段归属及距离函数，必须在新坐标下重新评估；本次不修改C++系数，也不宣称placement或时序改善。已经取消的10ns布线实验不会恢复。

转换器对非连续tX、同一类型左右侧冲突、无法推断的单位置列、重叠/外部特殊资源列等不支持结构显式报错，不静默退回RPM。未知资源并非自动按1单位补列，需要增加架构识别规则和验证。

服务器重现示例，输出目录必须尚不存在：

```bash
python3 scripts/prepare_fabric_device.py \
  data/devices/u250-physical-v1/raw/sites.tsv \
  data/devices/u250-tile-columns-v3/exportSiteLocation.zip \
  --part xcu250-figd2104-2L-e \
  --metadata data/devices/u250-physical-v1/raw/metadata.tsv \
  --structure-sites data/devices/u250-physical-v1/raw/structure_sites.tsv \
  --structure-tiles data/devices/u250-physical-v1/raw/structure_tiles.tsv
python3 scripts/build_physical_boundaries.py \
  --raw-dir data/devices/u250-physical-v1/raw \
  --fabric data/devices/u250-tile-columns-v3/exportSiteLocation.zip \
  --coordinates data/devices/u250-tile-columns-v3/exportSiteLocation.coordinates.json \
  --rules configs/architectures/ultrascale-plus-boundaries.json \
  --out data/devices/u250-tile-columns-v3/model
```

## 旧版规则（已经核验）

证据脚本：experiments/preflight/u250-port-20260926-125223/source/benchmarks/VCU108/preprocessPython/exportDeviceLocation.py。

旧版从 tile 名称的 _X…Y… 提取 tX/tY，不读取 RPM_X/RPM_Y。构造示意矩形后将中心写入 centerx/centery；示意高度不等于硅片实测尺寸。

### X

1. X=tX；若X>=34，X+=1；接着若修改后的X>=53，X+=1。等价基础形式为 tX+I(tX>=34)+I(tX>=52)。
2. HRIO_L_X、HPIO_L_X 前缀的tile另外X+=1。
3. 普通左侧site中心为X−0.25，右侧为X+0.25。
4. 左右判断：tile包含_R_、DSP或以PCIE_X开头时为右，其余左；CLE_M_R_X84是强制左的特例。
5. GTH_R的BUFG_GT_SYNC另加0.15；GTY_QUAD_LEFT_FT相应资源减0.15。

34/52、X84等是旧器件特例，不能复制到U250；更不能直接用U250 tile COLUMN当作同尺度tX。

### Y

一般式：Ycenter=Ybase−0.15+yoffset+height/2。

gw=0.3；普通site高度0.3、yoffset=0。主要资源：

|资源|height|yoffset|最终导出Y|
|---|---:|---:|---|
|SLICE|0.3|0|tY|
|DSP下/上|2.075|0.0375 / 2.1875|tY+0.925 / tY+3.425|
|RAMBFIFO18|2.075|0.0375|tY+0.925+0.35×I(siteY为奇数)|
|RAMB181|2.075|2.1875|tY+3.075+0.35×I(siteY为奇数)|
|RAMBFIFO36|4.3|0|tY+2+0.35×I(siteY为奇数)|

0.35来自导出尾部对所有site名字包含DSP或RAMB且siteY为奇数的修正。当前旧数据中RAMB181为上半，导出后与下半间距2.5。RAMB36也受到奇偶修正；这是可复现的旧实现行为，不应未经物理验证就固化为新架构规则。

特殊资源的Ybase：

- HPIOB、HRIO、BITSLICE_RX_TX：floor(siteY/26)*30+((siteY%26)+1)/28*30。
- BITSLICE_CONTROL、BITSLICE_TX：floor(siteY/4)*30+((siteY%4)+0.5)/5*30。
- RIU_OR：floor(siteY/2)*30+((siteY%2)+0.5)/3*30。
- PLLE3_ADV：tY+13。
- BUFCE_LEAF_X16：按siteY奇偶在Ybase加入1/3或2/3，再加gw/2−gw*0.3/2；高度0.09。
- BUFCE_ROW：Ybase加入1/2+gw/2−gw*0.3/2；高度0.09。
- GTH_R、GTY_QUAD_LEFT_FT：按tile聚合、按siteY排序；COMMON上移(59+gw)/2−1；CHANNEL围绕COMMON设置−5、−3、+3、+5；BUFG_GT与SYNC另按脚本的固定分组间距分布。这些是器件专用启发式，未纳入本轮主要资源数值核验。


## 旧规则核验记录

`diagnostics/audit_legacy_coordinates.py` 已核验旧主要资源共 73152 个 site（包括服务器 faceDetect 正式基线），与旧脚本规则零不一致，最大浮点误差约 5.7e-14。旧规则用于解释与复现，不作为 U250 的常量来源。
