#!/usr/bin/env python3
"""Reconcile AMF runtime scopes and export non-overlapping functional costs."""
import argparse
from collections import defaultdict
import csv
from datetime import datetime
import hashlib
import json
from pathlib import Path

CATEGORIES = {
    'input_device': '器件读取与物理结构建模', 'input_netlist': '网表读取与设计建模',
    'initialization': '配置与初始化其余工作', 'initial_packing': '初始宏识别与打包',
    'clustering': '簇构建与映射', 'partition': '超图递归划分及等待', 'sa': '模拟退火初始布局',
    'qp_model': 'QP 目标构建与坐标回写', 'qp_solve_wait': 'QP X/Y 并行求解及等待',
    'qp_solver': 'QP 求解其余工作', 'qp_validation': 'QP 输入校验',
    'qp_matrix': 'QP 稀疏矩阵组装', 'qp_guard': 'QP 数值保护',
    'qp_cg_prepare': 'CG 预处理', 'qp_cg_iterations': 'CG 迭代求解',
    'qp_acceptance': 'QP 解验收与回写', 'spreading': '过密区域扩散',
    'density_update': '密度与资源利用率调整', 'macro_legalization': '硬宏合法化',
    'clb_legalization': 'CLB 合法化', 'bipartite_matching': '二分图匹配',
    'incremental_packing': 'LUT/FF 增量配对', 'timing': '时序图与 STA',
    'boundary_clustering': '物理区域二维聚拢', 'boundary_capacity': '区域容量检查与预留',
    'final_packing': '最终 CLB 打包与 BEL 分配', 'detailed_placement': '时序驱动详细布局',
    'placement_bookkeeping': 'PU/网格/网络数据维护',
    'global_orchestration': '全局布局其余控制与未细分工作',
    'placement_orchestration': '顶层布局其余控制与未细分工作',
    'export_vivado': 'AMF→Vivado 文件导出', 'diagnostic_output': '诊断、报告与中间文件输出',
    'validation': '布局/资源/级联检查', 'cleanup': '对象释放', 'process_control': '进程其余控制',
    'partition_child': 'PaToH 子进程其余工作', 'partition_allocate': 'PaToH 分配',
    'partition_solve': 'PaToH 核心划分',
}
NON_PLACEMENT = {'input_device', 'input_netlist', 'initialization', 'export_vivado',
                 'diagnostic_output', 'validation', 'cleanup', 'process_control'}


def read_rows(path):
    with path.open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    for row in rows:
        for key in ('thread_index', 'os_tid', 'site_id', 'line', 'calls'):
            row[key] = int(row[key])
        for key in ('inclusive_wall_s', 'self_wall_s', 'inclusive_thread_cpu_s', 'self_thread_cpu_s', 'max_wall_s'):
            row[key] = float(row[key])
        if any(row[k] < 0 for k in ('self_wall_s', 'self_thread_cpu_s')):
            raise ValueError('Negative self time')
        if row['self_wall_s'] > row['inclusive_wall_s'] + 1e-7:
            raise ValueError('Self time exceeds inclusive time')
    return rows


def write_csv(path, rows, fields):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader(); writer.writerows(rows)


def summarize(run):
    run = Path(run).resolve()
    manifest = json.loads((run/'manifest.json').read_text())
    stage = next(s for s in manifest['stages'] if s['name'] == 'amf')
    if stage['exit_code'] != 0:
        raise ValueError('AMF did not exit successfully')
    report = run/'reports'
    profile = report/'amf_profile.tsv'
    meta = json.loads(Path(str(profile)+'.meta.json').read_text())
    if not meta['complete']:
        raise ValueError('Profile has unclosed scopes')
    rows = read_rows(profile)
    main_rows = [r for r in rows if r['thread_index'] == 0]
    entry = [r for r in main_rows if r['category'] == 'process_control' and 'main(' in r['function']]
    if len(entry) != 1:
        raise ValueError('Expected one main scope')
    main_wall = entry[0]['inclusive_wall_s']
    accounted_wall = sum(r['self_wall_s'] for r in main_rows)
    reconciliation = accounted_wall - main_wall
    if abs(reconciliation) > 1e-5:
        raise ValueError('Main-thread scopes do not reconcile: '+str(reconciliation))

    categories = defaultdict(lambda: dict(main_self_wall_s=0., measured_thread_self_cpu_s=0., calls=0))
    functions = {}
    build = Path(manifest['binary']).parent.parent
    for row in rows:
        c = categories[row['category']]
        c['calls'] += row['calls']; c['measured_thread_self_cpu_s'] += row['self_thread_cpu_s']
        if row['thread_index'] == 0: c['main_self_wall_s'] += row['self_wall_s']
        file = row['file']
        try: file = 'src/'+str(Path(file).relative_to(build/'src'))
        except ValueError: pass
        key = (file, row['line'], row['function'])
        if key not in functions:
            functions[key] = dict(file=file, line=row['line'], function=row['function'], category=row['category'],
                                 calls=0, main_inclusive_wall_s=0., main_self_wall_s=0.,
                                 worker_inclusive_wall_sum_s=0., worker_self_wall_sum_s=0.,
                                 all_thread_inclusive_cpu_s=0., all_thread_self_cpu_s=0., max_call_wall_s=0.)
        f = functions[key]; f['calls'] += row['calls']
        prefix = 'main_' if row['thread_index'] == 0 else 'worker_'
        tail = '_wall_s' if row['thread_index'] == 0 else '_wall_sum_s'
        f[prefix+'inclusive'+tail] += row['inclusive_wall_s']
        f[prefix+'self'+tail] += row['self_wall_s']
        f['all_thread_inclusive_cpu_s'] += row['inclusive_thread_cpu_s']
        f['all_thread_self_cpu_s'] += row['self_thread_cpu_s']
        f['max_call_wall_s'] = max(f['max_call_wall_s'], row['max_wall_s'])
    category_rows = [dict(category=k, description=CATEGORIES.get(k,k), **v,
                          main_wall_percent=100*v['main_self_wall_s']/main_wall) for k,v in categories.items()]
    category_rows.sort(key=lambda v:-v['main_self_wall_s'])
    function_rows = sorted(functions.values(),key=lambda v:(-v['main_self_wall_s'],-v['all_thread_self_cpu_s']))
    write_csv(report/'profile_categories.csv', category_rows, list(category_rows[0]))
    write_csv(report/'profile_functions.csv', function_rows, list(function_rows[0]))

    # Inventory also exposes sites that were compiled but did not execute.
    active = {(f['file'],f['line']) for f in function_rows}
    sites = []
    for source in sorted((build/'src').rglob('*')):
        if source.suffix not in ('.h','.cc','.cpp') or '/3rdParty/' in str(source) or '/tests/' in str(source):
            continue
        if source.name.startswith('RuntimeProfiler'): continue
        for line,text in enumerate(source.read_text(errors='replace').splitlines(),1):
            if 'AMF_PROFILE_FUNCTION(' in text or 'AMF_PROFILE_NAMED(' in text or 'AMF_PROFILE_SCOPE(' in text:
                relative = 'src/'+str(source.relative_to(build/'src'))
                sites.append(dict(file=relative,line=line,scope=text.strip(),executed=(relative,line) in active))
    write_csv(report/'profile_instrumentation_coverage.csv',sites,['file','line','scope','executed'])

    child_summaries, child_rows = [], []
    for path in sorted(report.glob('amf_profile.tsv.partition-*.tsv')):
        cm = json.loads(Path(str(path)+'.meta.json').read_text())
        if not cm['complete']: raise ValueError('Incomplete PaToH profile: '+str(path))
        cr = read_rows(path)
        child_summaries.append(dict(file=path.name,metadata=cm))
        for row in cr: child_rows.append(dict(pid=cm['pid'],**row))
    if child_rows: write_csv(report/'profile_partition_children.csv',child_rows,list(child_rows[0]))
    cpu = meta['process_user_s'] + meta['process_system_s']
    measured_cpu = sum(r['self_thread_cpu_s'] for r in rows)
    excluded = {k:v['main_self_wall_s'] for k,v in categories.items() if k in NON_PLACEMENT}
    placement = main_wall - sum(excluded.values())
    result = dict(schema='amf-functional-runtime-summary-v1',created=datetime.now().astimezone().isoformat(),
        run_id=run.name, binary=manifest['binary'],binary_sha256=manifest['binary_sha256'],
        profile_sha256=hashlib.sha256(profile.read_bytes()).hexdigest(), metadata=meta,
        amf_process_wall_s=stage['elapsed_seconds'],main_scope_wall_s=main_wall,
        main_self_wall_sum_s=accounted_wall,reconciliation_error_s=reconciliation,
        outside_main_and_profile_flush_s=stage['elapsed_seconds']-main_wall,
        placement_functional_wall_s=placement,non_placement_categories_s=excluded,
        measured_thread_cpu_s=measured_cpu,process_cpu_s=cpu,
        measured_cpu_coverage_percent=100*measured_cpu/cpu if cpu else None,
        cpu_unattributed_s=cpu-measured_cpu,
        categories=category_rows,function_count=len(function_rows),instrumentation_sites=len(sites),
        executed_sites=sum(s['executed'] for s in sites),partition_children=child_summaries,
        caveats=[
            'Wall-clock categories use main-thread exclusive time and sum to main wall; worker wall times overlap and must not be added.',
            'CPU uses per-thread clocks; uninstrumented OpenMP/library threads and profiler overhead remain unattributed.',
            'Function inclusive times overlap parents/children. Self time includes uninstrumented callees.',
            'PaToH child CPU is separate from the parent process CPU; child walls overlap one another and the parent wait.',
            'This is functional-scope instrumentation, not timings of every STL/Eigen/inline helper. Unexecuted scopes are listed.',
            'Instrumentation and shared-server load can affect timing and nondeterministic placement; no constant overhead subtraction was applied.',
            'Original DCP export was cached. Vivado import/place/route were not executed in this profiling run.'
        ])
    (report/'profile_summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    lines=['# GETRF / U250：AMF 功能运行时间 Profiling','',f'运行：`{run.name}`。',
           '',f'AMF 进程墙钟 **{stage["elapsed_seconds"]/60:.2f} 分钟**；主线程计时范围 **{main_wall/60:.2f} 分钟**。',
           f'按功能分类扣除输入、输出、检查、初始化和释放后，布局工作 **{placement/60:.2f} 分钟**；含布局调度、并行等待与尚未细分的布局子调用。',
           f'主线程排他时间求和误差 {reconciliation:.9f} 秒。已执行 {result["executed_sites"]}/{len(sites)} 个主程序编译计时点，导出 {len(function_rows)} 条函数/模板实例汇总。',
           '', '## 不重复累计的功能分布','',
           '| 功能 | 主线程排他墙钟（秒） | 比例 | 已归属各线程 CPU（秒） |', '|---|---:|---:|---:|']
    for row in category_rows:
        lines.append(f'| {row["description"]} | {row["main_self_wall_s"]:.3f} | {row["main_wall_percent"]:.2f}% | {row["measured_thread_self_cpu_s"]:.3f} |')
    lines += ['', 'CPU 列是各线程排他 CPU 之和，与墙钟不同；主线程等待时工作线程可能正在计算。',
              f'进程 CPU 合计 {cpu:.3f} 秒；已归属 {measured_cpu:.3f} 秒（{result["measured_cpu_coverage_percent"]:.2f}%）；其余 {cpu-measured_cpu:.3f} 秒保留为未归属，不任意分摊。',
              '', '## 文件与适配开销','']
    for category, seconds in excluded.items(): lines.append(f'- {CATEGORIES.get(category,category)}：{seconds:.3f} 秒。')
    lines += ['- DCP→AMF：本轮复用输入缓存，未执行。', '- Vivado 导入、placement、routing：本轮未执行；不得记为零耗时的已完成阶段。',
              f'- main 范围外、profiler 写出及进程启动退出：{stage["elapsed_seconds"]-main_wall:.3f} 秒。',
              '', '## PaToH 子进程','',f'共采集 {len(child_summaries)} 个独立子进程。子进程耗时与主线程划分等待重叠，不能再加到总墙钟。','']
    for category in sorted({row['category'] for row in child_rows}):
        cr=[row for row in child_rows if row['category']==category]
        lines.append(f'- {CATEGORIES.get(category,category)}：累计排他 CPU {sum(x["self_thread_cpu_s"] for x in cr):.3f} 秒；累计排他墙钟 {sum(x["self_wall_s"] for x in cr):.3f} 秒（含并行重叠）。')
    lines += ['', '## 全部函数明细','',
              '- `profile_functions.csv`：所有已执行计时函数的调用次数、包含/排他墙钟、CPU、最长单次调用及源码位置。',
              '- `profile_instrumentation_coverage.csv`：全部编译计时点，明确标记本轮未执行的功能。',
              '- `profile_partition_children.csv`：独立 PaToH 子进程明细。',
              '- `amf_profile.tsv`：保留线程编号的原始记录。',
              '', '## 解释边界','',
              '包含时间已经包含子函数，不能逐行累加。排他时间仍包含未单独插桩的内联函数、容器操作、库调用和部分日志输出；本报告没有声称逐个测量全部底层函数。',
              '本轮使用 profiling 构建，保持 -O3 和原算法配置。共享服务器负载、并行调度及插桩会影响运行时间与非确定性布局，不能直接把与历史轮次的耗时差当作算法改进。','']
    (report/'profile_summary.md').write_text('\n'.join(lines))
    return result


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('run',type=Path)
    result=summarize(p.parse_args().run)
    print(json.dumps({k:result[k] for k in ('run_id','amf_process_wall_s','placement_functional_wall_s','function_count','reconciliation_error_s')},indent=2))
