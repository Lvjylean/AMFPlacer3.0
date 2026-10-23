#!/usr/bin/env python3
"""Snapshot public AMF2 cases and fetch official projects; never run placement."""
import argparse
import collections
import concurrent.futures
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import time
import zipfile

import requests
from fetch_openparf_benchmarks import open_download

SOURCES = [
    ('face-detect', 'faceDetect', 'faceDetect', '10ZmeYW4b2oSkpu4rnDMG29kqwPJ8FKa0'),
    ('spoonn', 'halfsqueezenet', 'halfsqueezenet', '1LRg-HHw9Zir_V572_zzimhxik4FPOTWI'),
    ('blstm', 'BLSTM_midDensity', 'BLSTM_midDensity', '1XpWyHGnZIo71DkctqxEEht1clh5go6SE'),
    ('digit-recognition', 'digitRecognition', 'digitRecognition', '13wEQTSIW8CsKQeb23WbsntRGp2CF2voG'),
    ('memn2n', 'MemN2N', 'MemN2N', '1hGsxzdfVD9OaRRtxnqqOXju8A8X4AKOv'),
    ('openpiton', 'OpenPiton', 'OpenPiton', '1b0sWwoWq6XyiqWWxUxLlI9rAszVmR5WI'),
    ('optimsoc', 'optimsoc', 'optimsoc', '1Sx-ng7H-prkP6KbSuIT_DM_Hn0qa5fQv'),
    ('minimap2', 'minimap_GENE', 'minimap2', '1Dp1nL9KYuBgBjU2-1eL3IzYpl4OFD7As'),
    ('gemmini', 'Gemmini', 'Gemmini', None),
]
INPUT_KEYS = ['vivado extracted design information file', 'vivado extracted device information file',
              'special pin offset info file', 'cellType2fixedAmo file', 'cellType2sharedCellType file',
              'sharedCellType2BELtype file', 'mergedSharedCellType2sharedCellType',
              'unpredictable macro file', 'fixed units file', 'designCluster', 'clock file']

def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    tmp.replace(path)

def snapshot(source, dest):
    before = sha(source)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        shutil.copy2(source, dest)
    if sha(dest) != before or sha(source) != before:
        raise RuntimeError('Snapshot differs or source changed: ' + str(source))
    return {'source': str(source), 'snapshot': str(dest), 'sha256': before, 'bytes': dest.stat().st_size}

def parse_config(path):
    # Upstream files contain line comments and trailing commas; active values are strings.
    pairs = re.findall(r'^\s*"([^"\n]+)"\s*:\s*("(?:[^"\\]|\\.)*")', path.read_text(), re.M)
    return {key: json.loads(value) for key, value in pairs}

def prepare_inputs(root, bundle, source):
    name, config_name, design_name, file_id = source
    case = {'name': name, 'upstream_config': config_name, 'upstream_design': design_name,
            'official_project_id': file_id, 'u250_ready': False, 'placement_executed': False}
    config_source = root / 'benchmarks/testConfig' / (config_name + '.json')
    config = parse_config(config_source)
    case['original_config'] = snapshot(config_source, bundle / 'cases' / name / 'original_config.jsonc')
    case['amf_clock_period_ns'] = float(config['ClockPeriod'])
    case['inputs'] = {}
    for key in INPUT_KEYS:
        if key not in config:
            continue
        source_path = (root / 'build' / config[key]).resolve()
        rel = source_path.relative_to(root / 'benchmarks')
        record = snapshot(source_path, bundle / 'amf-inputs' / rel)
        case['inputs'][key] = record
        config[key] = record['snapshot']
    netlist = Path(case['inputs'][INPUT_KEYS[0]]['snapshot'])
    types = collections.Counter()
    inventory = bundle / 'cases' / name / 'benchmark_cells.tsv'
    with zipfile.ZipFile(netlist) as z, inventory.open('w') as output:
        names = [n for n in z.namelist() if not n.endswith('/')]
        if len(names) != 1:
            raise RuntimeError('Expected a single netlist member')
        output.write('cell\tprimitive\n')
        with z.open(names[0]) as f:
            for raw in f:
                if raw.startswith(b'curCell=> '):
                    cell, primitive = raw.decode().strip()[10:].rsplit(' type=> ', 1)
                    output.write(cell + '\t' + primitive + '\n')
                    types[primitive] += 1
    case['cell_count'] = sum(types.values())
    case['primitive_counts'] = dict(types)
    case['clock_driver_count'] = len([x for x in Path(config['clock file']).read_text().splitlines() if x.strip()])
    case['fixed_unit_lines'] = len([x for x in Path(config['fixed units file']).read_text().splitlines() if x.strip()])
    case['device_specific_primitives'] = {k: v for k, v in types.items() if re.search(r'PCIE|GT[HEY]|MMCM|PLL|BITSLICE|IOBUF|IBUF|OBUF|PHY|SYSMON', k)}
    case['input_status'] = 'snapshotted_and_netlist_crc_read'
    # The config is for the original VCU108 inputs, not a U250 conversion.
    config['device'] = 'VCU108'
    save(bundle / 'cases' / name / 'inspect_config.json', config)
    save(bundle / 'cases' / name / 'case.json', case)
    return case

def download_project(bundle, case):
    name, file_id = case['name'], case['official_project_id']
    if not file_id:
        case['project_status'] = 'no_project_link_in_upstream_eight_case_release'
        return case
    url = 'https://drive.usercontent.google.com/download?id=' + file_id + '&export=download'
    archive = bundle / 'archives' / (name + '.zip')
    archive.parent.mkdir(parents=True, exist_ok=True)
    partial = archive.with_suffix('.zip.part')
    state_path = bundle / 'cases' / name / 'download_status.json'
    state = {'source_url': url, 'started_at': now()}
    def update(**values):
        state.update(values, updated_at=now())
        save(state_path, state)
    try:
        if not archive.exists():
            for attempt in range(1, 4):
                offset = partial.stat().st_size if partial.exists() else 0
                try:
                    update(state='downloading', bytes=offset, attempt=attempt)
                    with requests.Session() as session, open_download(session, url, offset) as response:
                        length = response.headers.get('Content-Length')
                        expected = offset + int(length) if length else None
                        update(expected_bytes=expected, content_disposition=response.headers.get('Content-Disposition'))
                        last = time.monotonic()
                        with partial.open('ab' if offset else 'wb') as out:
                            for block in response.iter_content(4 * 1024 * 1024):
                                out.write(block)
                                offset += len(block)
                                if time.monotonic() - last > 10:
                                    update(bytes=offset)
                                    last = time.monotonic()
                    if expected is not None and offset != expected:
                        raise RuntimeError('Download truncated')
                    partial.rename(archive)
                    break
                except Exception as error:
                    update(last_error=str(error))
                    if attempt == 3:
                        raise
        update(state='inventory_and_extract', bytes=archive.stat().st_size)
        extracted = []
        candidates = []
        with zipfile.ZipFile(archive) as z, (bundle / 'cases' / name / 'archive_inventory.jsonl').open('w') as inv:
            for info in z.infolist():
                if info.is_dir():
                    continue
                member = info.filename
                inv.write(json.dumps({'path': member, 'bytes': info.file_size, 'crc32': info.CRC}) + '\n')
                is_impl_dcp = bool(re.search(r'\.runs/impl_[^/]+/[^/]+\.dcp$', member, re.I))
                if not (is_impl_dcp or member.lower().endswith(('.xdc', '.xpr'))):
                    continue
                path = PurePosixPath(member)
                if path.is_absolute() or '..' in path.parts:
                    raise RuntimeError('Unsafe archive path')
                dest = bundle / 'projects' / name / member
                dest.parent.mkdir(parents=True, exist_ok=True)
                if not dest.exists():
                    with z.open(info) as src, dest.open('xb') as dst:
                        shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                if dest.stat().st_size != info.file_size:
                    raise RuntimeError('Extracted size mismatch')
                record = {'archive_member': member, 'path': str(dest), 'bytes': info.file_size, 'sha256': sha(dest)}
                extracted.append(record)
                if is_impl_dcp:
                    candidates.append(record)
        def priority(item):
            p = item['path'].lower()
            stage = 0 if p.endswith('_opt.dcp') else 1 if p.endswith('_placed.dcp') else 2 if p.endswith('_routed.dcp') else 3
            return (0 if '/impl_1/' in p else 1, stage, p)
        candidates.sort(key=priority)
        case['archive'] = {'path': str(archive), 'source_url': url, 'bytes': archive.stat().st_size, 'sha256': sha(archive)}
        case['extracted_files'] = extracted
        case['dcp_candidates'] = candidates
        case['selected_dcp'] = candidates[0] if candidates else None
        case['project_status'] = 'downloaded_and_selected_members_crc_verified' if candidates else 'downloaded_no_top_implementation_dcp_found'
        update(state='completed', selected_dcp=case['selected_dcp'])
    except Exception as error:
        case['project_status'] = 'failed'
        case['error'] = str(error)
        update(state='failed', error=str(error))
    save(bundle / 'cases' / name / 'case.json', case)
    print(name + ': ' + case['project_status'], flush=True)
    return case

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    root, bundle = args.root.resolve(), args.bundle.resolve()
    if not bundle.is_relative_to(root / 'data/reference'):
        raise RuntimeError('Bundle must be under project data/reference')
    if bundle.exists() and not args.resume:
        raise RuntimeError('Existing bundle requires explicit --resume')
    bundle.mkdir(parents=True, exist_ok=True)
    manifest = {'started_at': now(), 'source_repository': 'https://github.com/zslwyuan/AMF-Placer',
                'git_commit': subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
                'git_status': subprocess.check_output(['git','-C',str(root),'status','--short'],text=True),
                'source_script_sha256': sha(Path(__file__)), 'state': 'preparing', 'placement_executed': False,
                'original_target': 'VCU108/xcvu095', 'requested_future_target': 'U250',
                'u250_conversion_executed': False}
    save(bundle / 'manifest.json', manifest)
    started = time.monotonic()
    cases = [prepare_inputs(root, bundle, source) for source in SOURCES]
    # BLSTM_DSPDomain has no released clock file/config and is not a complete main case.
    extra = root / 'benchmarks/VCU108/design/BLSTM_DSPDomain'
    manifest['incomplete_extra_variant'] = {'name': 'BLSTM_DSPDomain', 'files': [snapshot(p, bundle/'extras/BLSTM_DSPDomain'/p.name) for p in sorted(extra.iterdir()) if p.is_file()], 'missing': ['testConfig', 'clock file']}
    save(bundle / 'manifest.json', manifest)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        cases = list(pool.map(lambda case: download_project(bundle, case), cases))
    cases.sort(key=lambda case: case['cell_count'])
    save(bundle / 'catalog.json', cases)
    manifest.update(finished_at=now(), elapsed_seconds=time.monotonic()-started,
                    state='prepared' if all(c.get('project_status') != 'failed' for c in cases) else 'prepared_with_download_failures',
                    case_count=len(cases), main_case_count=8)
    save(bundle / 'manifest.json', manifest)
    print(json.dumps({'bundle':str(bundle), 'state':manifest['state'], 'cases':[{k:c.get(k) for k in ['name','cell_count','amf_clock_period_ns','project_status']} for c in cases]},indent=2),flush=True)

if __name__ == '__main__':
    main()
