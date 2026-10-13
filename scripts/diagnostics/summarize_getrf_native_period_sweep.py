"""Collect already-running native GETRF clock experiments without rerunning Vivado."""
import argparse
import csv
import datetime as dt
import hashlib
import json
from pathlib import Path
import re


def read(path):
    return json.loads(path.read_text())


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def completed_metrics(run, period):
    status = read(run / 'status.json')
    manifest = read(run / 'manifest.json')
    summary = read(run / 'reports/summary.json')
    config = manifest['config']
    assert not config['amf_executed'] and not config['opt_design']
    assert config['vivado_version'] == '2026.1' and config['vivado_threads'] == 4
    assert config['override_core_clock'] and config['period_ns'] == period
    override = summary['core_clock_override']
    assert summary['core_clock_override_verified']
    assert override['source_period_ns'] == 10 and override['target_period_ns'] == period
    assert abs(override['target_waveform'][1] - period / 2) < 1e-6
    assert manifest['input_dcp_sha256'] == '6283aa4874b42d939a2b00e53cce77574fa03a26f45914364ab9ca632b42c031'
    audit = summary['constraint_audit']
    assert audit['core_period_ns'] == period and audit['clock_snapshot_unchanged']
    assert audit['input_fixed_locations_verified'] and audit['io_standards_unchanged']
    assert (run / 'reports/input_ports.tsv').read_bytes() == (run / 'reports/final_ports.tsv').read_bytes()
    util = (run / 'reports/utilization.rpt').read_text()
    sll = int(re.search(r'\|\s*Total SLLs Used\s*\|\s*(\d+)', util)[1])
    boundaries, directions = [], []
    for line in util.splitlines():
        b = re.match(r'\|\s*(SLR\d+ <-> SLR\d+)\s*\|\s*(\d+)\s*\|\s*\|\s*(\d+)\s*\|\s*([\d.]+)', line)
        if b:
            boundaries.append(dict(boundary=b[1], used=int(b[2]), available=int(b[3]), utilization_percent=float(b[4])))
        d = re.match(r'\|\s*(SLR\d+ -> SLR\d+)\s*\|\s*(\d+)\s*\|', line)
        if d:
            directions.append(dict(direction=d[1], used=int(d[2])))
    assert sum(b['used'] for b in boundaries) == sum(d['used'] for d in directions) == sll
    log = (run / 'logs/vivado.log').read_text(errors='replace')
    phases = [line for line in log.splitlines() if re.match(r'Phase [57]\.', line)
              and 'Checksum' not in line and ('Iteration' in line or 'Hold Fix Iter' in line)]
    stages = {s['name']: s['elapsed_seconds'] for s in summary['vivado_stages']}
    total = (dt.datetime.fromisoformat(status['finished']) - dt.datetime.fromisoformat(manifest['started'])).total_seconds()
    process = next(s['elapsed_seconds'] for s in manifest['stages'] if s['name'] == 'vivado')
    dcp = run / 'reports/vivado_routed.dcp'
    assert dcp.is_file() and status['output_dcp_sha256'] == summary['final_dcp_sha256']
    digest = hashlib.sha256()
    with dcp.open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            digest.update(block)
    assert digest.hexdigest() == summary['final_dcp_sha256']
    inventories = {}
    for stage in ('input', 'placed', 'routed'):
        with (run / 'reports' / (stage + '_primitive_counts.tsv')).open() as stream:
            inventories[stage] = {row['primitive']: int(row['count']) for row in csv.DictReader(stream, delimiter='\t')}
    changes = {key: {stage: counts.get(key, 0) for stage, counts in inventories.items()}
               for key in inventories['input'].keys() | inventories['routed'].keys()
               if inventories['input'].get(key, 0) != inventories['routed'].get(key, 0)}
    timing_text = (run / 'reports/timing_summary.rpt').read_text()
    result = dict(schema='getrf-native-period-result-v1', run_id=run.name,
                  target_period_ns=period, state='completed', tools=summary['tools'],
                  config=config, input_dcp_sha256=manifest['input_dcp_sha256'],
                  source_clock_override=override, constraint_audit=audit,
                  constraint_coverage={k: int(v) for k, v in re.findall(r'\d+\.\s+checking (\w+) \((\d+)\)', timing_text)},
                  qor=dict(timing=summary['timing'], routing_counts=summary['routing_counts'],
                           routing_complete=summary['routing_complete'], drc_counts=summary['drc_counts'],
                           total_slls_used=sll, sll_boundaries=boundaries, sll_directions=directions,
                           amf_weighted_hpwl=None),
                  implementation_verified=summary['implementation_verified'], timing_met=summary['timing_met'],
                  routing_iterations=dict(route_design_calls=log.count('VIVADO_NATIVE_STAGE_START route\n'),
                                          global_iterations=sum('Global Iteration' in s for s in phases),
                                          additional_hold_iterations=sum('Additional Iteration for Hold' in s for s in phases),
                                          post_hold_fix_iterations=sum('Hold Fix Iter' in s for s in phases), phase_lines=phases),
                  timing_accounting=dict(vivado_stages_s=stages, vivado_process_wall_s=process,
                                         full_flow_wall_s=total, actual_placement_s=stages['place'],
                                         routing_s=stages['route'],
                                         vivado_unsegmented_wall_s=process-sum(stages.values())),
                  replication_log=[s for s in log.splitlines() if '[Physopt 32-81]' in s and 'Replicated ' in s],
                  primitive_inventories=inventories, primitive_changes=changes,
                  placed_and_routed_inventory_identical=inventories['placed'] == inventories['routed'],
                  final_dcp_hash_independently_rechecked=True,
                  final_dcp=str(dcp), final_dcp_sha256=summary['final_dcp_sha256'], final_dcp_bytes=dcp.stat().st_size,
                  limitations=['Runs executed concurrently; do not treat observed runtime as an isolated-host benchmark.',
                               'OOC HD.CLK_SRC and external I/O delay gaps remain.',
                               'Native placement may replicate logic; no formal equivalence proof is claimed.'])
    save(run / 'reports/native_metrics.json', result)
    return result


def collect(root, launch_path):
    launch = read(launch_path)
    records = []
    for item in launch['runs']:
        if not item.get('run_id'):
            lines = Path(item['runner_log']).read_text().splitlines()
            paths = [s.strip() for s in lines if s.startswith(str(root / 'experiments/runs'))]
            if not paths:
                records.append(dict(target_period_ns=item['target_period_ns'], state='starting'))
                continue
            item['run_id'] = Path(paths[0]).name
        run = root / 'experiments/runs' / item['run_id']
        status = read(run / 'status.json')
        record = dict(target_period_ns=item['target_period_ns'], run_id=run.name, state=status['state'],
                      stage=(run / 'reports/current_stage.txt').read_text().strip(), status=status)
        if status['state'] == 'completed':
            record['metrics'] = completed_metrics(run, item['target_period_ns'])
        elif status['state'] == 'failed':
            record['error'] = status.get('error')
            record['log_tail'] = '\n'.join((run / 'logs/vivado.log').read_text(errors='replace').splitlines()[-80:])
            save(run / 'reports/native_failure_diagnosis.json', record)
        records.append(record)
    save(launch_path, launch)
    reference_ids = launch.get('comparison_runs', ['getrf-vivado2026-1-native-10ns-20261006-152615-279917'])
    columns = []
    for reference_id in reference_ids:
        reference = root / 'experiments/runs' / reference_id
        period = read(reference / 'config.json')['period_ns']
        columns.append((period, read(reference / 'reports/native_metrics.json')))
    columns += [(r['target_period_ns'], r.get('metrics')) for r in records]
    columns.sort(key=lambda item: item[0], reverse=True)
    doc = root / launch.get('document_path', 'docs/research/getrf-vivado2026-1-native-period-sweep-20261006.md')
    progress_file = launch_path.parent / 'sweep_progress.json'
    previous = read(progress_file) if progress_file.exists() else {}
    periods = [f"{r['target_period_ns']:g} ns" for r in records]
    headings = [('10 ns 对照' if period == 10 else f'{period:g} ns') for period, _ in columns]
    lines = ['# GETRF 原生 Vivado 2026.1：'+' / '.join(periods)+' 并行实验', '',
             '两轮使用与 10 ns 对照相同的 post-opt DCP，ap_clk 分别显式改为 '+' 和 '.join(periods)+'；其余 I/O、固定位置约束保留。每轮 4 线程，默认 place_design / route_design，未使用 AMF、未添加 opt_design 或显式 phys_opt_design。', '',
             '原始输入 SHA-256：'+launch['input_sha256']+'。DCP 原文件未改写；时钟修改前后写入 source_clocks.rpt、core_clock_override.json 和 clocks.rpt。', '',
             '计时单位：分钟。并行实验与共享主机负载会影响耗时；报告、输入审计和 DCP 写出不计入 placement / routing。', '',
             '| 指标 | '+' | '.join(headings)+' |', '| --- | '+' | '.join(['---:']*len(columns))+' |']
    for label, getter in [('WNS（ns）', lambda m: f"{m['qor']['timing']['wns_ns']:+.3f}"),
                          ('TNS（ns）', lambda m: str(m['qor']['timing']['tns_ns'])),
                          ('setup 违例端点', lambda m: str(m['qor']['timing']['setup_failing_endpoints'])),
                          ('Vivado placement', lambda m: f"{m['timing_accounting']['actual_placement_s']/60:.2f}"),
                          ('Vivado routing', lambda m: f"{m['timing_accounting']['routing_s']/60:.2f}"),
                          ('总墙钟', lambda m: f"{m['timing_accounting']['full_flow_wall_s']/60:.2f}"),
                          ('全局迭代 / 额外 hold', lambda m: f"{m['routing_iterations']['global_iterations']} / {m['routing_iterations']['additional_hold_iterations']}"),
                          ('SLL 实际资源使用', lambda m: str(m['qor']['total_slls_used']))]:
        lines.append('| '+label+' | '+' | '.join(getter(m) if m else '待完成' for _, m in columns)+' |')
    lines += ['', 'SLL 是资源使用量，不是唯一跨 SLR net 数；原生流程没有 AMF 加权 HPWL。OOC 的时钟来源和接口时序缺口仍保留。', '']
    for record in records:
        lines += [f"## {record['target_period_ns']} ns", '',
                  '状态：'+record['state']+'；阶段：'+record.get('stage', 'starting')+'。', '']
        if record.get('run_id'):
            run = root / 'experiments/runs' / record['run_id']
            lines += ['运行目录：'+str(run), '配置：'+str(run/'config.json'),
                      '最终 DCP（完成后）：'+str(run/'reports/vivado_routed.dcp'), '']
        if 'error' in record:
            lines += ['失败：'+str(record['error']), '']
        if 'metrics' in record:
            m = record['metrics']
            lines += ['### 完整性、约束与网表审计', '',
                      '布通：'+str(m['qor']['routing_counts'])+'。',
                      'DRC：'+str(m['qor']['drc_counts'])+'；实现验收：'+str(m['implementation_verified'])+'；时序达标：'+str(m['timing_met'])+'。',
                      '时钟覆盖记录：'+json.dumps(m['source_clock_override'], ensure_ascii=False)+'。',
                      '最终约束审计：'+json.dumps(m['constraint_audit'], ensure_ascii=False)+'。',
                      '约束覆盖：'+json.dumps(m['constraint_coverage'], ensure_ascii=False)+'。',
                      'Primitive 类型变化：'+json.dumps(m['primitive_changes'], ensure_ascii=False)+'。',
                      'placed/routed 类型清单一致：'+str(m['placed_and_routed_inventory_identical'])+'。',
                      '默认 placer 可复制逻辑；计数变化与日志用于追溯，不作为形式化等价性证明。',
                      '最终 DCP SHA-256（重新计算核对）：'+m['final_dcp_sha256']+'。', '',
                      '### SLL 边界与方向', '',
                      '| 边界 | 实际用量 | 可用量 | 使用率 |', '| --- | ---: | ---: | ---: |']
            for b in m['qor']['sll_boundaries']:
                lines.append(f"| {b['boundary']} | {b['used']} | {b['available']} | {b['utilization_percent']:.2f}% |")
            lines += ['', '| 方向 | 实际用量 |', '| --- | ---: |']
            for b in m['qor']['sll_directions']:
                lines.append(f"| {b['direction']} | {b['used']} |")
            lines += ['', '### 耗时分解', '', '| 阶段 | 秒 | 分钟 |', '| --- | ---: | ---: |']
            names = dict(open='打开 DCP', retarget_clock='记录原时钟并显式覆盖周期', input_audit='输入审计',
                         place='Vivado placement', placed_reports='布局后报告', placed_checkpoint='布局 DCP 写出',
                         route='Vivado routing', routed_checkpoint='最终 DCP 写出', reports='最终报告和约束审计',
                         boundary_samples='边界时序诊断采样')
            account = m['timing_accounting']
            for key, value in account['vivado_stages_s'].items():
                lines.append(f"| {names.get(key,key)} | {value:.3f} | {value/60:.3f} |")
            for label, key in [('Vivado 未分段部分', 'vivado_unsegmented_wall_s'),
                               ('Vivado 进程墙钟（包含上述各项）', 'vivado_process_wall_s'),
                               ('总墙钟（包含上述各项）', 'full_flow_wall_s')]:
                value = account[key]
                lines.append(f"| {label} | {value:.3f} | {value/60:.3f} |")
            lines += ['', '重布线计数：'+json.dumps(m['routing_iterations'], ensure_ascii=False)+'。', '']
    body = '\n'.join(lines)+'\n'
    if doc.exists():
        assert hashlib.sha256(doc.read_bytes()).hexdigest() == previous.get('document_sha256'), 'Research document edited externally; preserve changes'
    doc.write_text(body)
    progress = dict(checked=dt.datetime.now().astimezone().isoformat(), runs=records,
                    all_terminal=all(r['state'] in ('completed', 'failed') for r in records),
                    document=str(doc), document_sha256=hashlib.sha256(doc.read_bytes()).hexdigest())
    save(progress_file, progress)
    for record in records:
        if record['state'] == 'completed':
            (root/'experiments/runs'/record['run_id']/'reports/native_comparison.md').write_text(body)
    return progress


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('/Projects/jinyang/workspace/AMFplacer3.0'))
    parser.add_argument('--launch', type=Path, required=True)
    args = parser.parse_args()
    progress = collect(args.root, args.launch)
    print(json.dumps(dict(all_terminal=progress['all_terminal'],
                          runs=[{k:r[k] for k in ('target_period_ns','state','stage','run_id') if k in r} for r in progress['runs']],
                          document=progress['document']), ensure_ascii=False))
