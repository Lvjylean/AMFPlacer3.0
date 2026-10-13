#!/usr/bin/env python3
"""Keep the full-board result distinct from OOC and strict-import acceptance."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re


def read(path):
    return json.loads(path.read_text()) if path.exists() else None


def run_record(path):
    manifest = read(path/'manifest.json')
    return {
        'path': str(path), 'status': read(path/'status.json'),
        'stages': manifest['stages'], 'import_policy': manifest['import_policy'],
        'input_dcp_sha256': manifest['input_dcp_sha256'],
        'binary_sha256': manifest['binary_sha256'],
        'coverage': read(path/'reports/amf_coverage.json'),
        'import_audit': read(path/'reports/imported_placement.json'),
        'placed_audit': read(path/'reports/placed_placement.json'),
        'routed_audit': read(path/'reports/routed_placement.json'),
        'summary': read(path/'reports/summary.json'),
    }


def timing_evidence(path):
    if not path.exists():
        return None
    content = path.read_text()
    checks = {name: int(count) for name, count in re.findall(
        r'^\d+\. checking (\w+) \((\d+)\)', content, re.M)}
    candidates = []
    for block in re.split(r'(?=^Slack \()', content, flags=re.M)[1:]:
        head = block.split('    Location', 1)[0]
        if not re.search(r'^  Path Type:\s+Setup', head, re.M):
            continue
        item = {'slack_ns': float(re.search(r'Slack .*?:\s+([-\d.]+)ns', head)[1])}
        for label, key in (('Source', 'source'), ('Destination', 'destination'), ('Path Group', 'clock')):
            item[key] = re.search(r'^  '+label+r':\s+([^\n]+)', head, re.M)[1].strip()
        item['requirement_ns'] = float(re.search(r'^  Requirement:\s+([\d.]+)ns', head, re.M)[1])
        delay = re.search(r'Data Path Delay:\s+([\d.]+)ns\s+\(logic ([\d.]+)ns \(([\d.]+)%\)\s+route ([\d.]+)ns \(([\d.]+)%\)\)', head)
        if delay:
            item.update(zip(('data_delay_ns', 'logic_delay_ns', 'logic_percent', 'route_delay_ns', 'route_percent'), map(float, delay.groups())))
        sections = re.split(r'^\s*-{8,}.*$', block, flags=re.M)
        if len(sections) > 2 and item['destination'] in sections[2]:
            data_section = sections[2]
            item['reported_data_slr_crossings'] = [list(map(int, pair)) for pair in re.findall(
                r'SLR Crossing\[(\d+)->(\d+)\]', data_section)]
            fanouts = [int(n) for n in re.findall(r'net \(fo=(\d+),', data_section)]
            item['max_data_net_fanout'] = max(fanouts) if fanouts else None
        candidates.append(item)
    return {'check_timing_counts': checks,
            'worst_reported_setup_path': min(candidates, key=lambda x: x['slack_ns']) if candidates else None}


def write_brief(result, path):
    summary = result['repair_run']['summary']
    if summary is None:
        return
    t, r, p = summary['timing'], summary['routing_counts'], summary['routed_placement']
    evidence = result['timing_evidence']
    lines = [
        'MiniMap2 完整设计：AMFplacer3.0 + U250',
        f"汇总时间：{result['updated']}",
        f"器件：{result['part']}",
        '范围：完整 PCIe/DMA、计算内核和板级 I/O；非 OOC 核心实验。',
        f"时钟：计算内核 {result['core_period_ns']} ns；PCIe 参考 {result['pcie_reference_period_ns']} ns；IP 内部时钟保持各自原约束。",
        'AMF 使用 R10 算法参数；新增器件资源兼容支持及逻辑区域几何保护，未据结果调参。',
        f"布线：{r['fully routed nets']}/{r['routable nets']}；DRC 错误 {summary['drc_counts']['errors']}，严重警告 {summary['drc_counts']['critical_warnings']}。",
        f"WNS {t['wns_ns']} ns；TNS {t['tns_ns']} ns；建立时间失败端点 {t['setup_failing_endpoints']}。",
        f"WHS {t['whs_ns']} ns；THS {t['ths_ns']} ns；保持时间失败端点 {t['hold_failing_endpoints']}。",
        f"WPWS {t['wpws_ns']} ns；时序达标：{summary['timing_met']}。",
        f"AMF 原始单元 {p['requested']} 个全部存在且放置；精确位置保留 {p['exact_loc_bel_matches']}/{p['requested']} ({100*p['exact_loc_bel_matches']/p['requested']:.4f}%)。",
        '严格导入未通过：PCIe/复位同步单元的 ASYNC_REG 分组约束与 AMF 位置发生冲突。',
        '最终结果为同一份 AMF 布局经 Vivado 修复后布线；时钟与 ASYNC_REG 约束未修改，不应标为严格导入通过。',
        '未进行 bitstream 或实际板卡功能测试。',
        f"时序约束检查：{json.dumps(evidence['check_timing_counts'], ensure_ascii=False)}",
        f"最差建立路径：{json.dumps(evidence['worst_reported_setup_path'], ensure_ascii=False)}",
        f"AMF 计时：{json.dumps(result['amf_runtime'], ensure_ascii=False)}",
        f"Vivado 分阶段秒数：{json.dumps(summary['vivado_stages_seconds'], ensure_ascii=False)}",
        f"严格导入运行：{result['strict_run']['path']}",
        f"修复后完整运行：{result['repair_run']['path']}",
        f"最终 DCP（仅服务器）：{result['final_dcp']['path']}",
        f"DCP SHA-256：{result['final_dcp']['sha256']}",
    ]
    path.write_text('\n'.join(lines)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('preparation', 'strict_run', 'repair_run', 'output'):
        parser.add_argument('--'+name.replace('_', '-'), type=Path, required=True)
    args = parser.parse_args()
    prep, strict, repair = (args.preparation.resolve(), args.strict_run.resolve(), args.repair_run.resolve())
    first, recovered = run_record(strict), run_record(repair)
    assert first['input_dcp_sha256'] == recovered['input_dcp_sha256']
    assert first['binary_sha256'] == recovered['binary_sha256']
    assert first['coverage']['missing_by_type'] == {}
    summary = recovered['summary']
    source = read(prep/'input_manifest.json')
    diagnosis = read(prep/'sync-constraints/import_diagnosis.json')
    if diagnosis:
        diagnosis.pop('edges', None)
    result = {
        'schema': 'minimap2-u250-full-experiment-v1',
        'updated': datetime.now().astimezone().isoformat(),
        'scope': source['scope'], 'part': source['part'],
        'core_period_ns': source['core_period_ns'],
        'pcie_reference_period_ns': source['pcie_reference_period_ns'],
        'original_project': source['original_project'],
        'input_provenance': read(prep/'amf-preparation-v2/input_provenance.json'),
        'clock_mapping': read(prep/'amf-preparation-v2/clock_mapping.json'),
        'synthesis': read(prep/'synthesis_status.json'),
        'board_and_inventory': read(prep/'board_preparation_retry4_status.json'),
        'input_conversion_and_inspection': read(prep/'amf_input_preparation_retry1_status.json'),
        'clock_geometry_validation': read(prep/'amf-compatibility/clock-fabric-geometry/retry1/geometry_comparison.json'),
        'amf_runtime': read(strict/'reports/minimap2_runtime_accounting.json'),
        'timing_evidence': timing_evidence(repair/'reports/timing_summary.rpt'),
        'strict_run': first, 'repair_run': recovered,
        'sync_constraint_diagnosis': diagnosis,
        'comparison_scope': {
            'r10_algorithm_parameters_preserved': True,
            'repair_reuses_the_same_amf_placement': True,
            'repair_changes_clock_or_async_reg_constraints': False,
            'strict_amf_import_passed': False,
            'final_implementation_verified': summary['implementation_verified'] if summary else None,
            'final_timing_met': summary['timing_met'] if summary else None,
            'hardware_tested': False,
            'note': 'Report Vivado repairs and measured placement retention separately; this is a complete PCIe/DMA system, not the earlier U250 OOC core result.'
        },
        'final_dcp': {
            'path': str(repair/'reports/getrf_routed.dcp'),
            'sha256': recovered['status'].get('output_dcp_sha256'),
            'storage': 'server-only',
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    write_brief(result, args.output.with_suffix('.txt'))
    print(json.dumps({'output': str(args.output.resolve()), 'backend_status': recovered['status']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
