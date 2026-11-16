#!/usr/bin/env python3
"""Render the human-readable rule catalog from its single JSON source."""
from pathlib import Path
import json
repo=Path(__file__).resolve().parents[2];out=repo/'docs/research'
data=json.loads((out/'slice-packing-rules.json').read_text())
rules=data['rules'];pairs=data['resource_pairs'];sources=data['sources']
lines=['# U250 Slice packing 规则台账（第一步）','',
'本台账把“硬件要求”“设计合同”“实现保守策略”和“尚未核实”分开。每个约束只有一个 ID；不同资源组合引用同一 ID，不复制规则正文。此处的完整性是**所有已声明分类与资源组合均有条目或明确未知项**，不是证明 AMD 全部底层互连规则已无遗漏。','',
'## 本步改动','',
'独立分支 `codex/slicem-ff-cohabitation-20261104`，来源 `3cb69db7cce1f65010c569f405259497160373a3`。新增显式开关 `"SLICEMFFPacking": "true"`；缺省为 false。保持评分清零修复，其他算法和硬件规则不改。','',
'支持足迹明确的 SRLC32E / RAM64X1S site 接受兼容的独立 FF。主槽是 AFF…HFF，副槽是 AFF2…HFF2；不是不同的逻辑 FF 类型。存储所在行的副 FF 只允许该存储输出，外部独立 D 保持拒绝。空 LUT 行为 FF 直通保留，暂不再放普通 LUT。','',
'原有 CLK/SR/CE 与极性、区域、锁定、Q 出口检查仍生效。候选和最终映射调用同一个位置证明函数；预先缓存 8 行存储足迹，候选最多检查 16 个 FF、24 个 bank 排列与每组至多 4! 槽位组合，无新增全设计扫描。分组仍沿用原算法，不保证找到全部理论可行划分。','',
'## 怎样做到不重复、可检查遗漏','',
'1. 规则以语义 ID 为唯一来源，Markdown 由 JSON 台账生成。','2. 按 site/BEL、LUT、控制、数据通路、存储模式、级联、设计合同、实现策略分层。','3. 六类资源的 21 个无序组合全部登记；跨三类资源及共享总线约束另列，不能把两两兼容误当充分条件。','4. 每条给出实现责任、证据和验证边界。未知项不能当作硬件禁止，也不自动放行。','5. 新放宽规则必须补正反物理证据和候选/最终落位回归；只改 JSON 不会改变运行行为。','',
'`tests/check_slice_rule_catalog.py` 检查 ID/语义键唯一、分类覆盖、21 对无重复无缺项、引用有效及未知状态。它检查台账结构，不证明未公开硬件事实。','',
'## 规则清单','',
'| ID | 类别/状态 | 唯一规则描述 | 实现责任与证据 |','|---|---|---|---|']
for x in rules:lines.append(f"| {x['id']} | {x['kind']} / {x['status']} | {x['predicate']} | `{x['owner']}`；{x['evidence']} |")
lines+=['','## 组合覆盖索引','','所有组合均附加 site/BEL、合同和出口约束。这里的列举不是“允许”列表。','','| 组合 | 引用规则 |','|---|---|']
for p in pairs:lines.append('| '+' + '.join(p['resources'])+' | '+', '.join(p['rules'])+' |')
lines+=['','## 物理与软件验证口径','','本次证据在独立实验根目录 `experiments/preflight/20261104-slicem-ff-cohabitation/validation/`。以该实验最终 `validation-final.json` 为结果入口；早期夹具错误保留，不计为硬件反例。','',
'Vivado 小 case 固定存储和 FF 的 LOC/BEL，检查布线后仍实际占据原 BEL、类型不变、内部可布线网络全部完成且 DRC Error/Critical Warning 为零。负例必须确认为目标位置冲突，不能把脚本异常算作预期拒绝。OOC 顶层端口隐式布线，不代表板级 I/O 或全设计时序验证。','',
'软件验证覆盖掩码和槽位匹配、实际 DesignInfo/InitialPacker/PackingCLBCluster 的试放—复制—移除—重插—最终落位，以及已有 CLB/FF 极性回归。尚未运行全量 GETRF/llama，未证明 CLB 数量下降，也未证明全设计时序或后端合法性。','',
'## 本步继续保留的边界','','SRL 每 site 四个的策略、复合 RAM、双 SRL16、存储+普通 LUT、带 MUX 存储宏、固定/锁定 PU、FF 宏和 native/localPinAccess 模式均未放宽。存储内部共享关系沿用已有合法化，不因 FF 共存而重新拼 RAM/SRL 宏。','',
'## 官方来源','']
for k,v in sources.items():lines.append(f'- [{k}: UG574]({v})')
(out/'slice-packing-rules.md').write_text('\n'.join(lines)+'\n')
