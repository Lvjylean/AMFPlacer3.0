#!/usr/bin/env python3
"""Render all measured AMF functional scopes, preserving wall/CPU distinctions."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path


def render(run):
    run = Path(run).resolve()
    report = run / 'reports'
    summary = json.loads((report / 'profile_summary.json').read_text())
    with (report / 'profile_functions.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == summary['function_count']
    categories = {c['category']: c['description'] for c in summary['categories']}
    groups = defaultdict(list)
    for row in rows:
        groups[row['category']].append(row)
    lines = [
        '# AMF 全部已测量功能函数', '',
        f'运行：`{run.name}`；共 {len(rows)} 条函数/模板实例汇总。', '',
        '“自身墙钟”已扣除同线程内已计时的子调用，仍包含未细分的库函数、等待和日志。'
        '“包含墙钟”包含子调用，不可逐行求和。所有线程 CPU 自身时间可相加，'
        '但不能与墙钟相加；主线程墙钟为 0 的函数可能仅在工作线程执行。', '',
        '完整数值与工作线程墙钟见 `profile_functions.csv`，未执行计时点见 '
        '`profile_instrumentation_coverage.csv`。PaToH 子进程单独列在 '
        '`profile_partition_children.csv`，不计入以下主进程函数表。', '',
        f'源码路径相对于冻结构建 `{Path(summary["binary"]).parent.parent}`。', '',
    ]
    for category in sorted(groups, key=lambda k: -sum(float(r['main_self_wall_s']) for r in groups[k])):
        lines += [f'## {categories.get(category, category)}', '',
                  '| 函数 / 计时范围 | 调用次数 | 主线程自身墙钟 s | 主线程包含墙钟 s | 各线程自身 CPU s | 最长单次墙钟 s | 源码 |',
                  '|---|---:|---:|---:|---:|---:|---|']
        for row in groups[category]:
            name = row['function'].replace('|', '&#124;')
            source = row['file'] + ':' + row['line']
            lines.append(f'| `{name}` | {row["calls"]} | '
                         f'{float(row["main_self_wall_s"]):.6f} | '
                         f'{float(row["main_inclusive_wall_s"]):.6f} | '
                         f'{float(row["all_thread_self_cpu_s"]):.6f} | '
                         f'{float(row["max_call_wall_s"]):.6f} | `{source}` |')
        lines.append('')
    target = report / 'profile_function_details.md'
    target.write_text('\n'.join(lines))
    manifest = {'schema': 'amf-profile-detail-render-v1', 'functions': len(rows),
                'inputs_sha256': {name: hashlib.sha256((report / name).read_bytes()).hexdigest()
                                  for name in ('profile_summary.json', 'profile_functions.csv')},
                'renderer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'output_sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    (report / 'profile_function_details.provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    print(render(parser.parse_args().run))
