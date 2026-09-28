#!/usr/bin/env python3
"""Run one strict GETRF flow and record a same-binary clustering comparison."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from summarize_sa_ratio_experiment import collect_run


def now():
    return datetime.now().astimezone().isoformat()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(config, root):
    result = {}
    for key, value in config.items():
        if key in ('dumpDirectory', 'DumpCLBPacking', 'BoundaryReportDirectory'):
            continue
        if isinstance(value, str) and value.startswith(str(root) + '/'):
            value = value[len(str(root)) + 1:]
        result[key] = value
    return result


def finish_report(root, directory, meta, exit_code, report_stem='results', update_status=True):
    old = collect_run(root, meta['baseline'])
    new = collect_run(root, meta['candidate'])
    a, b = old['manifest'], new['manifest']
    ca, cb = normalize(old['config'], root), normalize(new['config'], root)
    delta = {k: [ca.get(k), cb.get(k)] for k in ca.keys() | cb.keys() if ca.get(k) != cb.get(k)}
    checks = {
        'same_binary': a['binary_sha256'] == b['binary_sha256'],
        'same_dcp': a['input_dcp_sha256'] == b['input_dcp_sha256'],
        'same_inputs': a['inputs'] == b['inputs'],
        'strict_import_policy': a['import_policy'] == b['import_policy'] == 'strict',
        'only_clustering_toggle': delta == {'BoundaryAwareClustering': ['false', 'true']},
        'same_backend_script': a.get('backend_script_sha256') == b.get('backend_script_sha256') if b.get('backend_script_sha256') else None,
        'same_runner_files': a.get('runner_files_sha256') == b.get('runner_files_sha256') if b.get('runner_files_sha256') else None,
    }
    result = dict(schema='getrf-clustering-comparison-v1', collected_at=now(),
                  baseline=old, candidate=new, comparability=checks, config_delta=delta,
                  driver_exit_code=exit_code,
                  caveats=['Historical R08 baseline; server load was not isolated.',
                           'One GETRF run; current OOC constraints are retained.'])
    save(directory / (report_stem + '.json'), result)
    lines = ['# GETRF / U250：0.71 比例下二维聚拢对照', '',
             '基线为 R08；本轮共享与 SA 比例均为 0.71，10 ns，严格导入，使用相同冻结二进制。', '',
             f"本轮状态：{new['status']['state']}；驱动退出码：{exit_code}。", '',
             '| 指标 | R08：关闭二维聚拢 | 本轮：开启二维聚拢 |', '|---|---:|---:|']
    metrics = [('WNS（ns）', 'timing', 'wns_ns', 1), ('TNS（ns）', 'timing', 'tns_ns', 1),
               ('setup 违例端点', 'timing', 'setup_failing_endpoints', 1),
               ('AMF 运行：扣除已计时 Tcl 导出（分钟）', 'timing_accounting', 'amf_run_excluding_recorded_tcl_export_seconds', 60),
               ('AMF→Vivado Tcl 导出（秒，已计时部分）', 'timing_accounting', 'amf_recorded_tcl_export_seconds', 1),
               ('Vivado 布局导入：适配开销（分钟）', 'seconds', 'import', 60),
               ('Vivado 打开 DCP（分钟）', 'seconds', 'open', 60),
               ('Vivado placement（分钟）', 'seconds', 'place', 60),
               ('Vivado routing（分钟）', 'seconds', 'route', 60),
               ('总墙钟（分钟）', 'seconds', 'total_wall', 60)]
    for label, group, key, scale in metrics:
        vals = []
        for run in (old, new):
            value = run.get(group, {}).get(key)
            vals.append('—' if value is None else str(value) if key == 'setup_failing_endpoints'
                        else f'{value / scale:.2f}' if scale == 60 else f'{value:.3f}')
        lines.append('| ' + label + ' | ' + ' | '.join(vals) + ' |')
    iterations = [run.get('routing_global_iterations') for run in (old, new)]
    lines += ['| 全局拆线重布轮数 | ' + ' | '.join(str(len(x)) if x else '—' for x in iterations) + ' |', '',
              '格式适配不计入 placement。AMF 净运行只扣除日志有完整起止记录的 Tcl 导出，仍含未完整分段的输入解析、诊断及其他输出；不能称为纯算法时间。原始 AMF 进程墙钟保存在 JSON 的 seconds.amf_process。', '',
              '总墙钟不含原始综合、首次网表导出和构建；本轮复用网表缓存，DCP→AMF 导出未执行。routing 包含内部迭代。审计、报告、DCP 写出和采样属于其他开销。未完成轮次不提供最终时序。', '',
              'DCP（仅服务器）：', '']
    for label, run in (('R08', old), ('本轮', new)):
        lines.append(f"- {label}：`{run['routed_dcp']}`" if run.get('routed_dcp') else f'- {label}：尚无最终 DCP。')
    lines += ['', '可比性检查：', '', '```json', json.dumps(checks, ensure_ascii=False, indent=2), '```', '']
    (directory / (report_stem + '.md')).write_text('\n'.join(lines))
    if update_status:
        save(directory / 'status.json', dict(state=new['status']['state'], finished=now(),
             driver_exit_code=exit_code, run_id=meta['candidate'], timing_met=new['status'].get('timing_met'),
             implementation_verified=new['status'].get('implementation_verified'), report=str(directory / (report_stem + '.md'))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--comparison-dir', type=Path, required=True)
    args = parser.parse_args()
    root, directory = args.root.resolve(), args.comparison_dir.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    save(directory / 'status.json', dict(state='preflight', started=now()))
    try:
        old_dir = root / 'experiments/runs' / args.baseline
        old_manifest = json.loads((old_dir / 'manifest.json').read_text())
        source = (root / args.config).resolve()
        source_bytes = source.read_bytes()
        config = json.loads(source_bytes)
        old = normalize(json.loads((old_dir / 'config.json').read_text()), root)
        new = normalize(config, root)
        delta = {k: [old.get(k), new.get(k)] for k in old.keys() | new.keys() if old.get(k) != new.get(k)}
        if delta != {'BoundaryAwareClustering': ['false', 'true']}:
            raise ValueError('Unexpected configuration differences: ' + json.dumps(delta))
        assert config['PhysicalBoundaryMode'] == 'true'
        assert float(config['ClockPeriod']) == 10
        assert float(config['y2xRatio']) == float(config['Simulated Annealing y2xRatio']) == 0.71
        binary = Path(old_manifest['binary'])
        assert sha(binary) == old_manifest['binary_sha256']
        build = binary.parent.parent
        hashes = json.loads((build / 'source_hashes.json').read_text())
        mismatches = [name for name, expected in hashes.items()
                      if not (root / 'src' / name).is_file() or sha(root / 'src' / name) != expected]
        if mismatches:
            raise ValueError('Source differs from R08 frozen build: ' + json.dumps(mismatches))
        snapshot = directory / 'config-source.json'
        snapshot.write_bytes(source_bytes)
        (directory / 'supervisor.py').write_bytes(Path(__file__).read_bytes())
        (directory / 'collector.py').write_bytes(Path(__file__).with_name('summarize_sa_ratio_experiment.py').read_bytes())
        command = [sys.executable, '-u', 'scripts/amf3.py', 'full-run', '--config', str(snapshot), '--binary', str(binary)]
        meta = dict(created=now(), baseline=args.baseline, candidate=None, config_source=str(source),
                    config_source_sha256=sha(snapshot), config_delta=delta, command=command,
                    binary=str(binary), binary_sha256=sha(binary), source_files_verified=len(hashes),
                    dcp_storage='server-only')
        save(directory / 'comparison.json', meta)
        save(directory / 'status.json', dict(state='running', stage='launch', started=now()))
        with (directory / 'driver.log').open('w') as log:
            proc = subprocess.Popen(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            meta['driver_pid'] = proc.pid
            save(directory / 'comparison.json', meta)
            for line in proc.stdout:
                log.write(line)
                log.flush()
                candidate = Path(line.strip())
                if not meta['candidate'] and line.startswith(str(root / 'experiments/runs') + '/') and candidate.is_dir():
                    meta['candidate'] = candidate.name
                    save(directory / 'comparison.json', meta)
                    save(candidate / 'inputs/comparison-launch.json', meta)
                    save(directory / 'status.json', dict(state='running', stage='full-flow', run_id=candidate.name))
                    print(json.dumps(dict(run_id=candidate.name, comparison_directory=str(directory))), flush=True)
            code = proc.wait()
        if not meta['candidate']:
            raise RuntimeError(f'Full-flow launcher failed before creating manifest; exit={code}')
        finish_report(root, directory, meta, code)
        return code
    except Exception as exc:
        save(directory / 'status.json', dict(state='failed', stage='supervisor', finished=now(), error=str(exc)))
        raise


if __name__ == '__main__':
    raise SystemExit(main())
