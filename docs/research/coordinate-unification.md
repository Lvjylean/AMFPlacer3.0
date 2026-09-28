# xcvu095 与 U250 坐标规则与实现

用户已确认：统一的是坐标生成规则和参考点语义；U250 按自身资源结构生成坐标，不要求数值或尺寸匹配 xcvu095。延迟系数后续单独校准。VCU108 的旧器件是 xcvu095，并非 Zynq 的 zu095。

## 本次修订

正式转换入口为 `scripts/prepare_fabric_device.py`，模型为 `normalized-rpm-x-tile-row-anchors-v2`。新导出存于服务器 `data/devices/u250-coordinate-rules-v2/`，配套物理模型在其 `model/` 下；使用配置 `configs/experiments/getrf-u250-coordinate-rules.json`。历史实验和输入保持可复现。

此前 `u250-legacy-shared-scale-v1-experimental` 候选误将统一理解为复现旧数值，现已停用。原入口明确报错，源码保存在 `archives/legacy-workspace/scripts/prepare_legacy_scale_device.py`，旧实验仅作追溯。其 X×0.5、DSP/BRAM18 的 +0.925、BRAM36 的 +2 与奇偶 +0.35 均不进入新规则。

## 统一的生成规则

参考点统一为资源行起始锚点。输入格式仍沿用 `centerx/centery` 字段名，但这不是硅片几何中心，也不是微米或已标定的传播时间。

设 pX 为全器件不同 SLICE RPM_X 相邻差的众数，x0 为 fabric 的最小 RPM_X，则：

`X(site) = (RPM_X(site) - x0) / pX`。

设 pY 为不同 SLICE tile_Y 相邻差的众数，y0 为最小 SLICE tile_Y。一个资源 tile 的起点为 tY，跨度为 h 个 SLICE 行，同一资源族在其中有 n 个 site，按 site_Y 递增排序，序号 i 从 0 开始：

`Y(site) = (tY - y0) / pY + i * h / n`。

h 来自同资源列相邻 tile 的实际起点间隔，并与末 tile 到当前 clock region 的 SLICE 上边界间隔校验；n 来自每 tile 的真实 site 数。CR 编号用于分组，不再以 `CR_Y * 60` 代替实际位置。物理 tile 坐标中的间隔保留，不将缺失资源均分压缩。

当前转换器适用规则资源列：SLICE 参考列完整，资源 tile 等跨度、等 site 数且覆盖所在 CR。遇到不完整导出、非连续 site 编号、不一致 tile 跨度、列内 RPM_X 变化或 RPM/site 排序冲突会报错；不能据此声称支持所有未来 FPGA 的不规则资源列。

## U250 真实数据得到的参数

Part：`xcu250-figd2104-2L-e`，Vivado 2024.2。x0=48、pX=16、y0=0、pY=1；每 CR 为 60 个 SLICE 行，4 个 SLR。这些值来自数据，不为匹配 095 设定。

|资源|tile 跨度 h|每 tile 同族 site 数 n|tile 内 Y 偏移|连续同列资源间距|
|---|---:|---:|---|---:|
|SLICE|1|1|0|1|
|DSP48E2|5|2|0、2.5|2.5|
|RAMB18|5|2|0、2.5|2.5|
|RAMB36|5|1|0|5|
|URAM288|15|4|0、3.75、7.5、11.25|3.75|

RAMB36 与下半 RAMB18 共用行起始锚点；两个 RAMB18 表示同一 BRAM tile 的上下半，不能与 RAMB36 当成独立容量重复累加。现有打包器用 BRAM18Height=2.5、BRAM36Height=5 构造宏，与此约定相容。DSP 间距从器件数据读取。此次没有修改 C++、pin 偏移或延迟模型。

本次全部 237632 个 fabric site 的坐标与原始 U250 导出数值一致；新实现增加实际 tile 几何来源、版本标识和异常检查。与已否定的缩放候选相比，X 恢复原尺度，Y 去除人为中心偏移。HPIO 带 X=158，SLR 接缝 Y=239.5/479.5/719.5，均通过配套模型重新生成。

## 验证与限制

独立验证目录：服务器 `experiments/preflight/20260928-u250-coordinate-rules-v2/`。本地仅同步轻量证据到 `experiments/evidence/20260928-u250-coordinate-rules-v2/`。

- 全部 site 身份、坐标、类型、BEL、CR、SLR、禁用属性与原 U250 导出一致。
- 按独立 U250 公式检查全部坐标；检查 2688 个 BRAM tile 的 18K/36K 锚点关系。
- 重新生成物理边界，检查源文件、坐标、器件与模型哈希绑定。
- 回归测试覆盖资源间距、禁用属性、拒绝覆写、不规则 tile、缺失资源、RPM 列冲突、物理 tile 间隙保留与旧候选入口停用。
- GETRF 原生输入检查通过（退出 0，29.01 秒），硬资源合法化通过（退出 0，77.17 秒）；6 项针对性 Python 测试通过。结果见 validation-manifest.json。使用已有冻结二进制，未将其声称为当前 dirty 源码的重新构建。

不以这些检查代替完整 CLB 布局、Vivado 路由或时序校准。保持 ClockPeriod=10 ns 和现有延迟系数；统一坐标规则不证明历史回归系数可复用。095 旧导出保持历史复现语义；若以后将 095 也迁移到新规则，需要重新导出其 RPM/tile 数据，不能直接宣称旧示意中心已采用新锚点规则。

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
