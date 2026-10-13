from pathlib import Path
import csv,datetime,hashlib,json,re,shutil
root=Path('/Projects/jinyang/workspace/AMFplacer3.0')
native=root/'experiments/runs/getrf-vivado2026-1-native-10ns-20261006-152615-279917'
def load(p): return json.loads(p.read_text())
status=load(native/'status.json')
if status['state']!='completed':
 print(json.dumps({'state':status,'stage':(native/'reports/current_stage.txt').read_text().strip()}))
 raise SystemExit(0)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for block in iter(lambda:f.read(8<<20),b''):h.update(block)
 return h.hexdigest()
manifest=load(native/'manifest.json');summary=load(native/'reports/summary.json')
assert manifest['config']['amf_executed'] is False and manifest['config']['opt_design'] is False
assert manifest['config']['preserve_input_constraints'] and manifest['config']['vivado_threads']==4
assert summary['tools']['vivado_version']=='2026.1'
assert summary['implementation_verified'] and summary['routing_complete'] and summary['timing_met']
assert summary['drc_counts']=={'errors':0,'critical_warnings':0}
assert summary['release_placement']['amf_placement_imported'] is False
assert summary['constraint_audit']['core_period_ns']==10
assert summary['constraint_audit']['clock_snapshot_unchanged'] and summary['constraint_audit']['io_standards_unchanged']
assert summary['constraint_audit']['input_fixed_locations_verified']
assert (native/'reports/input_ports.tsv').read_bytes()==(native/'reports/final_ports.tsv').read_bytes()
util=(native/'reports/utilization.rpt').read_text()
sll=int(re.search(r'\|\s*Total SLLs Used\s*\|\s*(\d+)',util).group(1))
boundaries=[];directions=[]
for line in util.splitlines():
 b=re.match(r'\|\s*(SLR\d+ <-> SLR\d+)\s*\|\s*(\d+)\s*\|\s*\|\s*(\d+)\s*\|\s*([\d.]+)',line)
 if b:boundaries.append({'boundary':b[1],'used':int(b[2]),'available':int(b[3]),'utilization_percent':float(b[4])})
 d=re.match(r'\|\s*(SLR\d+ -> SLR\d+)\s*\|\s*(\d+)\s*\|',line)
 if d:directions.append({'direction':d[1],'used':int(d[2])})
assert len(boundaries)==3 and len(directions)==6
assert sum(x['used'] for x in boundaries)==sll==sum(x['used'] for x in directions)
timtext=(native/'reports/timing_summary.rpt').read_text()
checks={k:int(v) for k,v in re.findall(r'\d+\.\s+checking (\w+) \((\d+)\)',timtext)}
row=timtext.split('WNS(ns)',1)[1].splitlines()[2].split()
assert float(row[0])==summary['timing']['wns_ns'] and int(row[2])==summary['timing']['setup_failing_endpoints']
log=(native/'logs/vivado.log').read_text(errors='replace')
phases=[l for l in log.splitlines() if re.match(r'Phase [57]\.',l) and 'Checksum' not in l and ('Iteration' in l or 'Hold Fix Iter' in l)]
iterations={'route_design_calls':log.count('VIVADO_NATIVE_STAGE_START route\n'),'global_iterations':sum('Global Iteration' in l for l in phases),'additional_hold_iterations':sum('Additional Iteration for Hold' in l for l in phases),'post_hold_fix_iterations':sum('Hold Fix Iter' in l for l in phases),'phase_lines':phases}
assert iterations['route_design_calls']==1
stages={s['name']:s['elapsed_seconds'] for s in summary['vivado_stages']}
assert all(s['tcl_status']==0 for s in summary['vivado_stages'])
process=next(s['elapsed_seconds'] for s in manifest['stages'] if s['name']=='vivado')
assert all(s['exit_code']==0 for s in manifest['stages'])
total=(datetime.datetime.fromisoformat(status['finished'])-datetime.datetime.fromisoformat(manifest['started'])).total_seconds()
account={'vivado_stages_s':stages,'vivado_process_wall_s':process,'full_flow_wall_s':total,'vivado_unsegmented_wall_s':process-sum(stages.values()),'outside_vivado_wall_s':total-process,'actual_placement_s':stages['place'],'routing_s':stages['route'],'amf_executed':False,'amf_format_adaptation_applicable':False,'note':'Report and checkpoint phases excluded from placement/routing. Process wall and full wall include child stages; do not sum twice.'}
def inventory(name):
 with (native/'reports'/name).open() as f:return {r['primitive']:int(r['count']) for r in csv.DictReader(f,delimiter='\t')}
counts={stage:inventory(stage+'_primitive_counts.tsv') for stage in ['input','placed','routed']}
deltas={k:{stage:v.get(k,0) for stage,v in counts.items()} for k in counts['input'].keys()|counts['routed'].keys() if counts['input'].get(k,0)!=counts['routed'].get(k,0)}
replication=[l for l in log.splitlines() if 'Replicated ' in l and 'times.' in l]
assert counts['placed']==counts['routed']
dcp=native/'reports/vivado_routed.dcp';dcp_sha=sha(dcp)
assert dcp_sha==status['output_dcp_sha256']==summary['final_dcp_sha256']
assert sha(Path(manifest['input_dcp']))==manifest['input_dcp_sha256']
baselines=[]
for label,rid in [('AMF+Vivado 2024.2（原 R10）','getrf-u250-full-20260929-133345-334380'),('AMF+Vivado 2026.1','getrf-r10-vivado2026-1-10ns-full-20261006-132647-570065')]:
 r=root/'experiments/runs'/rid;m=load(r/'reports/r10_metrics.json');mf=load(r/'manifest.json')
 assert mf['input_dcp_sha256']==manifest['input_dcp_sha256']
 baselines.append({'label':label,'run_id':rid,'metrics':m})
now=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat()
sample=load(native/'reports/physical/timing_sample_analysis.json')
metrics={'schema':'getrf-native-comparison-v1','created':now,'run_id':native.name,'state':'completed','input_dcp':manifest['input_dcp'],'input_dcp_sha256':manifest['input_dcp_sha256'],'tools':summary['tools'],'config':manifest['config'],'qor':{'timing':summary['timing'],'routing_counts':summary['routing_counts'],'routing_complete':True,'drc_counts':summary['drc_counts'],'drc':summary['drc'],'total_slls_used':sll,'sll_boundaries':boundaries,'sll_directions':directions,'amf_weighted_hpwl':None,'hpwl_note':'Native flow does not emit AMF coordinate/ratio weighted HPWL; not zero.'},'routing_iterations':iterations,'timing_accounting':account,'constraint_audit':summary['constraint_audit'],'constraint_coverage':checks,'primitive_inventories':counts,'primitive_changes':deltas,'replication_log':replication,'boundary_sampling':{k:sample[k] for k in ['unique_driver_sink_samples','missing_site_rows','sampling_gaps']},'baselines':baselines,'final_dcp':str(dcp),'final_dcp_sha256':dcp_sha,'final_dcp_bytes':dcp.stat().st_size,'limitations':['Same input and 10 ns/4 threads; native Vivado may change the logic during place_design.','OOC HD.CLK_SRC and external I/O delay coverage remains incomplete.','No native GETRF Vivado 2024.2 control is recorded; this is not an isolated version-upgrade experiment.','Native and AMF reporting/audit work differs; compare explicit placement/routing as well as total wall.']}
t=[b['metrics']['timing_accounting'] for b in baselines]
q=[b['metrics']['qor'] for b in baselines];nt=summary['timing']
lines=['# GETRF：原生 Vivado 2026.1 / 10 ns 最终对照','',
'状态：已完成；记录时间 '+now+'。原生 place_design、route_design、报告、约束验收和最终 DCP 均已完成。未调用 AMF，也未导入 AMF 布局。','',
'## 主要结果','',
'| 指标 | AMF+2024.2（原 R10） | AMF+2026.1 | 原生 Vivado 2026.1 |','| --- | ---: | ---: | ---: |',
f"| WNS（ns） | {q[0]['timing']['wns_ns']:+.3f} | {q[1]['timing']['wns_ns']:+.3f} | {nt['wns_ns']:+.3f} |",
f"| TNS（ns）/ setup 违例 | {q[0]['timing']['tns_ns']:.3f} / {q[0]['timing']['setup_failing_endpoints']} | {q[1]['timing']['tns_ns']:.3f} / {q[1]['timing']['setup_failing_endpoints']} | {nt['tns_ns']:.3f} / {nt['setup_failing_endpoints']} |",
f"| WHS（ns）/ hold 违例 | +0.020 / 0 | +0.020 / 0 | {nt['whs_ns']:+.3f} / {nt['hold_failing_endpoints']} |",
f"| AMF 实际布局（分钟） | {t[0]['amf_placement_functional_wall_s']/60:.2f} | {t[1]['amf_placement_functional_wall_s']/60:.2f} | 不适用 |",
f"| Vivado place_design（分钟） | {t[0]['vivado_placement_s']/60:.2f} | {t[1]['vivado_placement_s']/60:.2f} | {stages['place']/60:.2f} |",
f"| 实际布局合计（分钟） | {(t[0]['amf_placement_functional_wall_s']+t[0]['vivado_placement_s'])/60:.2f} | {(t[1]['amf_placement_functional_wall_s']+t[1]['vivado_placement_s'])/60:.2f} | {stages['place']/60:.2f} |",
f"| Vivado route_design（分钟） | {t[0]['vivado_routing_s']/60:.2f} | {t[1]['vivado_routing_s']/60:.2f} | {stages['route']/60:.2f} |",
f"| AMF→Vivado 布局导入（分钟） | {t[0]['vivado_import_s']/60:.2f} | {t[1]['vivado_import_s']/60:.2f} | 不适用 |",
f"| 总墙钟（分钟） | {t[0]['full_flow_wall_s']/60:.2f} | {t[1]['full_flow_wall_s']/60:.2f} | {total/60:.2f} |",
f"| 实际 SLL 使用量 | {q[0]['total_slls_used']:,} | {q[1]['total_slls_used']:,} | {sll:,} |",
'| AMF 加权 HPWL | 3,190,300.817061 | 3,190,300.817061 | 无此统计，不写作 0 |',
f"| Phase 5 全局重布线 / 额外 hold 迭代 | 3 / 0 | 1 / 1 | {iterations['global_iterations']} / {iterations['additional_hold_iterations']} |",
'| AMF 导入策略 | strict | repair | 不适用 |','',
f"相对同版本 AMF+Vivado 2026.1，本轮总墙钟减少 {(t[1]['full_flow_wall_s']-total)/60:.2f} 分钟（{(1-total/t[1]['full_flow_wall_s'])*100:.2f}%）；实际布局合计减少 {(t[1]['amf_placement_functional_wall_s']+t[1]['vivado_placement_s']-stages['place'])/60:.2f} 分钟；routing 耗时变化 {(stages['route']-t[1]['vivado_routing_s'])/60:+.2f} 分钟。WNS 差值 {nt['wns_ns']-q[1]['timing']['wns_ns']:+.3f} ns。以上为本例实测，不是普遍性能结论。",'',
'本轮与 AMF 两组使用同一原始 DCP、ap_clk=10 ns、4 线程和默认 place_design/route_design，均未额外运行 opt_design。原生 place_design 自带的物理优化仍可能修改网表，见下文；因此不属于相同最终网表上的纯坐标算法对比。','',
'当前正式项目没有 GETRF 原生 Vivado 2024.2 对照，不能把原生 2026.1 与 AMF+2024.2 的差异全部解释为版本升级。未新增 2024.2 实验。','',
'## 完整性与约束验收','',
f"- 需要布线网络 {summary['routing_counts']['routable nets']:,}，全部布通 {summary['routing_counts']['fully routed nets']:,}，布线错误 {summary['routing_counts']['nets with routing errors']}。",
f"- DRC Error {summary['drc_counts']['errors']}、Critical Warning {summary['drc_counts']['critical_warnings']}；其余严重度计数：{json.dumps(summary['drc']['totals'],ensure_ascii=False)}。不等于 DRC 零告警。",
f"- setup 端点 {nt['setup_total_endpoints']:,}，违例 {nt['setup_failing_endpoints']}；hold 端点 {nt['hold_total_endpoints']:,}，违例 {nt['hold_failing_endpoints']}。",
'- 输入与最终时钟周期/波形、I/O standards 和端口位置审计通过；input_ports.tsv 与 final_ports.tsv 逐字节一致；原始固定位置审计通过。',
f"- check_timing：{json.dumps(checks,ensure_ascii=False)}。",
'- 继续沿用 OOC 约束：ap_clk 的 HD.CLK_SRC 未设置，外部 I/O delay 仍有缺口；端口 HD.PARTPIN_LOCS 相关告警亦保留。因此不能扩大为板级签核或完整功能等价性证明。','',
'## 原生物理优化导致的网表变化','',
'Primitive 清单采用排除嵌套内部 primitive 的口径，不应直接与 AMF 审计的全部 get_cells IS_PRIMITIVE 数量混用。','',
'| 类型 | 输入 | placed | routed |','| --- | ---: | ---: | ---: |']
for k,v in sorted(deltas.items()):lines.append(f"| {k} | {v['input']:,} | {v['placed']:,} | {v['routed']:,} |")
lines+=['',
'日志记录：','']
lines += ['- '+s for s in replication]
lines+=['',
'本轮 placement 后与 routing 后类型清单完全相同，增加的 FDRE 与默认 placement 中的驱动复制记录相符；未人为开启额外 phys_opt_design。类型清单一致/变化可解释不等于形式化逻辑等价性证明。AMF 两轮日志未记录同样的复制事件，因此不能只按 cell 坐标差异解释全部 QoR。','',
'## SLL 实际资源使用','',
'| 边界 | 使用量 | 可用量 | 使用率 |','| --- | ---: | ---: | ---: |']
for v in boundaries:lines.append(f"| {v['boundary']} | {v['used']:,} | {v['available']:,} | {v['utilization_percent']:.2f}% |")
lines+=['','| 方向 | 使用量 |','| --- | ---: |']
for v in directions:lines.append(f"| {v['direction']} | {v['used']:,} |")
lines+=['',f'合计 {sll:,}；边界和方向合计均与 utilization.rpt 的 Total SLLs Used 核对一致。它是资源使用量，不是唯一跨 SLR net 数。','',
'## 原生完整耗时','',
'| 阶段 | 墙钟秒 | 墙钟分钟 |','| --- | ---: | ---: |']
names={'open':'打开 DCP','input_audit':'输入检查/时钟/约束导出','place':'原生 place_design','placed_reports':'布局后报告','placed_checkpoint':'placed DCP 写出','route':'route_design','routed_checkpoint':'routed DCP 写出','reports':'最终报告与约束审计','boundary_samples':'边界时序样本'}
for k,v in stages.items():lines.append(f"| {names.get(k,k)} | {v:.3f} | {v/60:.3f} |")
lines += [f"| Vivado 未分段部分 | {account['vivado_unsegmented_wall_s']:.3f} | {account['vivado_unsegmented_wall_s']/60:.3f} |",
f"| Vivado 进程墙钟（包含以上） | {process:.3f} | {process/60:.3f} |",
f"| Vivado 进程外流程开销 | {account['outside_vivado_wall_s']:.3f} | {account['outside_vivado_wall_s']/60:.3f} |",
f"| 完整总墙钟 | {total:.3f} | {total/60:.3f} |",'',
'总墙钟由 manifest.started 到 status.finished；报告、审计、DCP 写出和边界诊断各自列出。原生与 AMF 流程的报告工作不同，总墙钟和纯 placement/routing 应分别比较。原生没有 AMF 文件适配；输入是 post-opt DCP，不含前端综合和先前 opt_design。AMF 输入导出在对照轮复用缓存，不能把其历史成本伪造为 0。共享主机负载没有严格控制，不做推测扣除。','',
f"路由完整调用 {iterations['route_design_calls']} 次；Phase 5 全局迭代 {iterations['global_iterations']} 轮、额外 hold {iterations['additional_hold_iterations']} 轮，Phase 7 Post Hold Fix {iterations['post_hold_fix_iterations']} 轮。内层迭代均已包含在 route_design 耗时内，不再相加。",'']
lines += ['- '+s for s in phases]
lines+=['',
'## 配置、位置与证据','',
'本轮开始：'+manifest['started']+'；结束：'+status['finished']+'。',
'版本：Vivado 2026.1，SW Build 6511674；可执行文件 /Projects/Xilinx26/2026.1/Vivado/bin/vivado。',
'器件：xcu250-figd2104-2L-e；输入 SHA-256：'+manifest['input_dcp_sha256']+'。',
'运行目录：'+str(native),
'启动配置：'+str(native/'config.json'),
'冻结实现脚本：'+str(native/'inputs/vivado_baseline.tcl'),
'最终 DCP（仅服务器）：'+str(dcp),
'最终 DCP SHA-256：'+dcp_sha,
'最终 DCP 字节数：'+str(dcp.stat().st_size),'',
'原 R10 DCP：'+str(root/'experiments/runs/getrf-u250-full-20260929-133345-334380/reports/getrf_routed.dcp'),
'同版本 AMF DCP：'+str(root/'experiments/runs/getrf-r10-vivado2026-1-10ns-full-20261006-132647-570065/reports/getrf_routed.dcp'),'',
'本轮 reports/native_metrics.json 保存完整结构化统计及对照；原始 summary.json、stages.tsv、tools.json、constraint_audit.json、timing_summary.rpt、route_status.rpt、utilization.rpt、primitive 清单与 logs/vivado.log 保留。启动证据位于 experiments/evidence/getrf-native-vivado2026-launch-20261006-152445。完整命令见 reports/launch.json。','',
'结果未触发参数更改或重新布局布线。DCP 与大型数据仅在服务器保留，本地只同步白名单报告、日志、配置和本研究文档。']
nc=summary['routing_counts']
bc=load(root/'experiments/runs/getrf-r10-vivado2026-1-10ns-full-20261006-132647-570065/reports/summary.json')['routing_counts']
note=(f"布线统计口径：原生 logical nets={nc['logical nets']:,}，AMF 对照={bc['logical nets']:,}，增加 {nc['logical nets']-bc['logical nets']}，与新增 FDRE 复制一致。原生 internally routed nets={nc['internally routed nets']:,}，对照={bc['internally routed nets']:,}，增加 {nc['internally routed nets']-bc['internally routed nets']:,}；因此 routable nets 从 {bc['routable nets']:,} 变为 {nc['routable nets']:,}。两轮各自需要路由的网络均全量布通，不能把 routable 计数减少误判为遗漏网络。")
index=lines.index('## 原生物理优化导致的网表变化')
lines[index:index]=[note,'']
body='\n'.join(lines)+'\n'
doc=root/'docs/research/getrf-vivado2026-1-native-10ns.md'
backup=native/'inputs/launch_research_document.md'
if not backup.exists():shutil.copy2(doc,backup)
(native/'reports/native_metrics.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2)+'\n')
(native/'reports/native_comparison.md').write_text(body)
(native/'reports/native_experiment.md').write_text(body)
doc.write_text(body)
(native/'reports/monitor_progress.json').write_text(json.dumps({'state':'completed','checked':now,'metrics':'reports/native_metrics.json','dcp_sha256':dcp_sha},indent=2)+'\n')
print(json.dumps({'state':'completed','timing':nt,'sll':sll,'boundaries':boundaries,'directions':directions,'timing_accounting':account,'routing_counts':summary['routing_counts'],'drc':summary['drc'],'iterations':iterations,'primitive_changes':deltas,'dcp_sha256':dcp_sha,'local_previous_doc_sha256':sha(backup),'doc':str(doc)},ensure_ascii=False,indent=2))
