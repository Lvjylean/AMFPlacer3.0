"""Build a part-bound nominal clock-capacity model before placement.

Physical track availability, the AMF BBox budget, and design occupancy are
different quantities. This module never invents reservations or a routed state.
"""
import csv
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile

TRACKS = ('hroute', 'hdistr', 'vroute', 'vdistr')
REGION = re.compile(r'X([0-9]+)Y([0-9]+)')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def read_metadata(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        fields = line.split('\t')
        if len(fields) != 2 or not all(fields) or fields[0] in result:
            raise ValueError('Invalid or duplicate metadata: ' + line)
        result[fields[0]] = fields[1]
    return result


def select_rule(rules, part, family=None):
    if rules.get('schema') != 'amf-clock-architecture-rules-v1':
        raise ValueError('Unsupported clock architecture rules schema')
    matches = [r for r in rules['profiles'] if re.fullmatch(r['part_pattern'], part.lower())]
    if len(matches) != 1:
        raise ValueError('No unique validated clock architecture rule for part: ' + part)
    rule = matches[0]
    if family and family.lower() not in rule['family_values']:
        raise ValueError('Vivado family does not match clock architecture rule: ' + family)
    for value in [*rule['nominal_tracks'].values(), rule['half_column_limit'], rule['bbox_clock_limit']]:
        if type(value) is not int or not 0 <= value <= 1024:
            raise ValueError('Invalid architecture capacity')
    if set(rule['nominal_tracks']) != set(TRACKS):
        raise ValueError('Incomplete architecture track capacities')
    return rule


def read_regions(path):
    result = {}
    with Path(path).open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            name = row['clock_region']
            if not REGION.fullmatch(name) or name in result or not re.fullmatch(r'[0-9]+', row['slr']):
                raise ValueError('Invalid or duplicate clock region: ' + name)
            result[name] = int(row['slr'])
    if not result:
        raise ValueError('Empty clock region inventory')
    return result


def archive_regions(device, raw_sites):
    """Bind the table to the actual AMF archive and cross-check fabric identities."""
    with Path(raw_sites).open() as f:
        inventory = {r['site']: r for r in csv.DictReader(f, delimiter='\t')}
    regions, seen = {}, set()
    pattern = re.compile(r'^site=>\s+(\S+).*?clockRegionName=>\s+(X[0-9]+Y[0-9]+).*?sitetype=>\s+(\S+)')
    with zipfile.ZipFile(device) as z:
        files = [n for n in z.namelist() if not n.endswith('/')]
        if len(files) != 1:
            raise ValueError('Expected one text device inventory in archive')
        with z.open(files[0]) as f:
            for raw in f:
                line = raw.decode('utf-8').strip()
                if not line:
                    continue
                m = pattern.search(line)
                if not m or m[1] in seen:
                    raise ValueError('Malformed or duplicate device site')
                name, cr, kind = m.groups()
                seen.add(name)
                sm = re.search(r'\bslr=>\s+([0-9]+)\b', line)
                slr = int(sm[1]) if sm else 0
                if cr in regions and regions[cr] != slr:
                    raise ValueError('Device archive has inconsistent SLR: ' + cr)
                regions[cr] = slr
                if name in inventory:
                    r = inventory[name]
                    if (r['clock_region'], int(r['slr']), r['site_type']) != (cr, slr, kind):
                        raise ValueError('Device site disagrees with queried part: ' + name)
                elif name.startswith(('SLICE_', 'DSP48', 'RAMB18_', 'RAMB36_', 'URAM')):
                    raise ValueError('Fabric site absent from queried part: ' + name)
    if not seen or not regions:
        raise ValueError('Empty device archive')
    missing = set(inventory) - seen
    if missing:
        raise ValueError('Device archive omits queried fabric sites, e.g. ' + min(missing))
    return regions


def report_track_capacities(path):
    """Read Avail, never Used, from the standard four-resource CR report table.

    No per-CR table is a valid preplacement reporting gap, not zero capacity.
    """
    result = {}
    if not Path(path).is_file():
        return result
    active = False
    for line in Path(path).read_text().splitlines():
        if all(word in line for word in ('HROUTES', 'HDISTRS', 'VROUTES', 'VDISTRS')):
            labels = [x.strip() for x in line.strip('|').split('|') if x.strip()]
            if labels != ['HROUTES', 'HDISTRS', 'VROUTES', 'VDISTRS']:
                raise ValueError('Unsupported clock track report column order')
            active = True
            continue
        if not active or not line.startswith('|'):
            continue
        fields = [x.strip() for x in line.strip('|').split('|')]
        if not fields or not REGION.fullmatch(fields[0]):
            continue
        if len(fields) != 13 or fields[0] in result:
            raise ValueError('Unsupported or duplicate clock utilization capacity row')
        try:
            values = [int(fields[i]) for i in (2, 5, 8, 11)]
        except ValueError as error:
            raise ValueError('Noninteger clock track availability') from error
        if any(v < 0 for v in values):
            raise ValueError('Negative clock track capacity')
        result[fields[0]] = dict(zip(TRACKS, values))
    return result


def build_table(raw_dir, device, part, rules_path, out_dir, half_column_raw=None):
    raw_dir, device, rules_path, out = map(Path, (raw_dir, device, rules_path, out_dir))
    names = ('clock_capacity.tsv', 'clock_capacity.json', 'config_overlay.json')
    if any((out / n).exists() for n in names):
        raise ValueError('Refusing to overwrite a clock capacity artifact')
    metadata = read_metadata(raw_dir / 'metadata.tsv')
    if metadata.get('part') != part or metadata.get('availability_source') != 'empty-device-design':
        raise ValueError('Nominal capacities require a matching empty-device export')
    clock_meta = read_metadata(raw_dir / 'clock_metadata.tsv')
    if (clock_meta.get('part') != part or clock_meta.get('schema_version') != '1' or
            clock_meta.get('availability_source') != 'empty-device-design' or
            clock_meta.get('placement_run_by_exporter') != '0' or
            clock_meta.get('vivado') != metadata.get('vivado')):
        raise ValueError('Clock metadata part/version/preplacement state mismatch')
    rules = json.loads(rules_path.read_text())
    family = clock_meta.get('family')
    rule = select_rule(rules, part, None if family in (None, 'unavailable') else family)
    regions = read_regions(raw_dir / 'clock_regions.tsv')
    if archive_regions(device, raw_dir / 'sites.tsv') != regions:
        raise ValueError('Device archive clock regions/SLRs differ from queried part')
    reported = report_track_capacities(raw_dir / 'preplacement_clock_utilization.rpt')
    if reported and set(reported) != set(regions):
        raise ValueError('Vivado report has incomplete clock region capacity coverage')
    for cr, values in reported.items():
        if values != rule['nominal_tracks']:
            raise ValueError('Vivado nominal track capacities disagree with architecture rule: ' + cr)
    # Validate the half-column architecture assumption against actual device sites.
    slice_columns = {}
    with (raw_dir / 'sites.tsv').open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            m = re.fullmatch(r'SLICE_X([0-9]+)Y([0-9]+)', row['site'])
            if m:
                slice_columns.setdefault((row['clock_region'], int(m[1])), []).append(int(m[2]))
    height = rule['slice_rows_per_clock_region']
    for key, ys in slice_columns.items():
        ys.sort()
        if len(ys) != height or ys != list(range(ys[0], ys[0] + height)):
            raise ValueError('Unsupported half-column geometry: ' + str(key))
    if not slice_columns or {cr for cr, _ in slice_columns} != set(regions):
        raise ValueError('Missing SLICE half-column geometry')
    half_limits = {cr: rule['half_column_limit'] for cr in regions}
    half_source = 'legacy-amf-ispd-placement-budget'
    half_topology = None
    if half_column_raw is not None:
        # Imported here to keep the legacy path independent of topology export,
        # and to avoid the analyzer's shared validation helpers forming a cycle.
        from analyze_clock_half_columns import analyze
        half_raw = Path(half_column_raw).resolve()
        topology_out = out / 'half_column_topology'
        topology = analyze(half_raw, raw_dir, device, topology_out)
        if (topology.get('part') != part or topology.get('vivado_version') != metadata['vivado'] or
                topology.get('device_sha256') != digest(device) or
                topology.get('resource_state') != 'nominal' or
                set(topology.get('regions', {})) != set(regions) or
                topology.get('clock_regions') != len(regions) or
                topology.get('slice_half_columns') != 2 * len(slice_columns)):
            raise ValueError('Half-column topology part/version/device/coverage mismatch')
        if not topology.get('amf_slice_half_column_partition_matches'):
            raise ValueError('Device leaf sharing differs from the AMF SLICE half-column grouping')
        for cr, entry in topology['regions'].items():
            capacities = entry.get('nominal_leaf_capacities', [])
            if (entry.get('slr') != regions[cr] or len(capacities) != 1 or
                    type(capacities[0]) is not int or not 0 < capacities[0] <= 1024):
                raise ValueError('Half-column capacities cannot be represented by one value per CR: ' + cr)
            half_limits[cr] = capacities[0]
        half_source = 'vivado-device-node-connectivity-endpoint-sampled'
        summary_path = topology_out / 'summary.json'
        half_topology = dict(raw_directory=str(half_raw), summary_path=str(summary_path.resolve()),
                             summary_sha256=digest(summary_path), sources=topology['sources'],
                             scope=topology['scope'],
                             endpoint_sampling=topology['metadata'].get('endpoint_sampling'),
                             geometry_assumptions=topology.get('geometry_assumptions'),
                             shared_leaf_domains=topology['shared_leaf_domains'],
                             clock_routability_verified=False)
    sources = {p.name: {'path': str(p.resolve()), 'sha256': digest(p)}
               for p in sorted(raw_dir.iterdir()) if p.is_file() and p.suffix in ('.tsv', '.rpt')}
    rows = []
    for cr in sorted(regions, key=lambda c: tuple(map(int, REGION.fullmatch(c).groups()))):
        rows.append(dict(clock_region=cr, slr=regions[cr],
                         nominal_tracks=reported.get(cr, rule['nominal_tracks']).copy(),
                         half_column_limit=half_limits[cr],
                         bbox_clock_limit=rule['bbox_clock_limit']))
    table = dict(schema='amf-clock-capacity-v1', part=part, architecture=rule['architecture'],
                 rules_version=rules['rules_version'], rule_id=rule['id'], resource_state='nominal',
                 vivado_version=metadata['vivado'], device_sha256=digest(device),
                 rules_sha256=digest(rules_path), rules_sources=rule['sources'], sources=sources,
                 track_capacity_source=('vivado-report-availability-verified-with-architecture-rule' if reported
                                        else 'architecture-rule-no-preplacement-track-table'),
                 half_column_capacity_source=half_source,
                 bbox_capacity_source='placement-model-rule-not-physical-track-count',
                 half_column_geometry=dict(scope='SLICE columns only; existing AMF mapping is unchanged',
                                           slice_rows_per_cr=height, nominal_half_columns=2 * len(slice_columns)),
                 design_occupancy=None, reserved_resources=None, clock_routability_verified=False,
                 notes=rule['notes'], regions=rows)
    if half_topology is not None:
        table['half_column_topology'] = half_topology
    lines = ['AMF_CLOCK_CAPACITY\t1']
    for key in ('part', 'architecture', 'rules_version', 'resource_state', 'vivado_version', 'device_sha256', 'rules_sha256'):
        lines.append('META\t' + key + '\t' + str(table[key]))
    lines.append('META\thalf_column_capacity_source\t' + half_source)
    if half_topology is not None:
        lines.append('META\thalf_column_topology_summary_sha256\t' + half_topology['summary_sha256'])
    for r in rows:
        lines.append('\t'.join(map(str, ['CR', r['clock_region'], r['slr'],
            *[r['nominal_tracks'][k] for k in TRACKS], r['half_column_limit'], r['bbox_clock_limit']])))
    out.mkdir(parents=True, exist_ok=True)
    (out / names[0]).write_text('\n'.join(lines) + '\n')
    table['table_sha256'] = digest(out / names[0])
    (out / names[1]).write_text(json.dumps(table, indent=2) + '\n')
    overlay = {'clock resource capacity file': str((out / names[0]).resolve()),
               'clock resource capacity part': part}
    (out / names[2]).write_text(json.dumps(overlay, indent=2) + '\n')
    return table


def validate_capacity_inputs(path, device, part):
    """Verify artifact binding before either inspection or full placement."""
    if not part:
        raise ValueError('Clock capacity table requires clock resource capacity part')
    lines = Path(path).read_text().splitlines()
    if not lines or lines[0] != 'AMF_CLOCK_CAPACITY\t1':
        raise ValueError('Unsupported clock capacity schema')
    meta = {}
    for line in lines[1:]:
        if line.startswith('META\t'):
            fields = line.split('\t')
            if len(fields) != 3 or fields[1] in meta:
                raise ValueError('Invalid clock capacity metadata')
            meta[fields[1]] = fields[2]
    if meta.get('part') != part or meta.get('resource_state') != 'nominal':
        raise ValueError('Clock capacity part/state mismatch')
    if meta.get('device_sha256') != digest(device):
        raise ValueError('Clock capacity table belongs to a different device archive')
    return meta


def validate_capacity_binary(binary):
    """Reject old binaries that otherwise silently ignore the new JSON keys."""
    binary = Path(binary).resolve()
    manifest = binary.parent.parent / 'manifest.json'
    if not manifest.is_file():
        raise ValueError('Clock capacity requires a recorded AMF build with capability metadata')
    record = json.loads(manifest.read_text())
    capability = record.get('capabilities', {})
    if (record.get('state') != 'completed' or
            capability.get('schema') != 'amf-capabilities-v1' or
            capability.get('clock_resource_capacity_schema') != 1 or
            record.get('binaries', {}).get('AMFPlacer') != digest(binary)):
        raise ValueError('Selected AMF binary does not have verified clock capacity support; rebuild with amf3.py')
    return capability


def prepare(root, args):
    from amf3 import git, machine, save, stamp
    root = Path(root)
    part = args.part
    query_half_columns = getattr(args, 'query_half_columns', False)
    half_column_topology_dir = getattr(args, 'half_column_topology_dir', None)
    if query_half_columns and half_column_topology_dir is not None:
        raise ValueError('Choose either a new half-column query or an existing topology directory')
    rules_path = (root / args.rules).resolve()
    # Fail before invoking Vivado for an unsupported target.
    select_rule(json.loads(rules_path.read_text()), part)
    vivado = str((root / (getattr(args, 'vivado', None) or machine()['vivado'])).resolve())
    device = (root / args.device).resolve()
    if not device.is_file():
        raise ValueError('Missing device archive: ' + str(device))
    directory = root / 'experiments/preflight' / ('clock-capacity-' + stamp())
    directory.mkdir(parents=True, exist_ok=False)
    exporter = root / 'scripts/export_fabric_device.tcl'
    source_files = [exporter, root / 'scripts/export_clock_resources.tcl', Path(__file__).resolve(),
                    rules_path, root / 'scripts/amf3.py']
    half_exporter = root / 'scripts/export_clock_half_column_topology.tcl'
    if query_half_columns or half_column_topology_dir is not None:
        source_files.extend((half_exporter, root / 'scripts/analyze_clock_half_columns.py'))
    record = dict(schema='amf-clock-capacity-preflight-v1', part=part,
                  source_commit=git('rev-parse', 'HEAD'), git_status=git('status', '--porcelain'),
                  started=dt.datetime.now().astimezone().isoformat(), placement_executed=False,
                  vivado_executable=vivado, device_archive=str(device), device_sha256=digest(device), stages=[],
                  source_hashes={str(p): digest(p) for p in source_files})
    save(directory / 'manifest.json', record)
    save(directory / 'status.json', dict(state='running'))
    print(str(directory), flush=True)
    begin = time.monotonic()
    try:
        snapshot = directory / 'source_snapshot'
        snapshot.mkdir()
        record['source_snapshot'] = {}
        for index, source in enumerate(source_files):
            copied = snapshot / (str(index) + '-' + source.name)
            shutil.copy2(source, copied)
            record['source_snapshot'][str(source)] = str(copied.relative_to(directory))
        if args.raw_dir:
            raw = (root / args.raw_dir).resolve()
            record['reused_export'] = str(raw)
        else:
            raw = directory / 'raw'
            command = [vivado, '-mode', 'batch', '-notrace', '-nojournal',
                       '-log', str(directory / 'vivado.log'), '-source', str(exporter),
                       '-tclargs', '--part', part, str(raw), '--clock-resources']
            start = time.monotonic()
            with (directory / 'console.log').open('w') as log:
                result = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
            record['stages'].append(dict(name='device-clock-export', command=command,
                elapsed_seconds=time.monotonic() - start, exit_code=result.returncode))
            if result.returncode:
                raise RuntimeError('Vivado device export failed; see ' + str(directory / 'console.log'))
        half_raw = None
        if half_column_topology_dir is not None:
            half_raw = (root / half_column_topology_dir).resolve()
            record['reused_half_column_topology'] = str(half_raw)
        elif query_half_columns:
            half_raw = directory / 'half_column_raw'
            command = [vivado, '-mode', 'batch', '-notrace', '-nojournal',
                       '-log', str(directory / 'half-column-vivado.log'), '-source', str(half_exporter),
                       '-tclargs', part, str(half_raw)]
            start = time.monotonic()
            with (directory / 'half-column-console.log').open('w') as log:
                result = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
            record['stages'].append(dict(name='device-half-column-topology-export', command=command,
                elapsed_seconds=time.monotonic() - start, exit_code=result.returncode))
            if result.returncode:
                raise RuntimeError('Vivado half-column query failed; see ' + str(directory / 'half-column-console.log'))
        start = time.monotonic()
        try:
            table = build_table(raw, device, part, rules_path, directory, half_column_raw=half_raw)
        except Exception:
            record['stages'].append(dict(name='build-clock-capacity-table',
                elapsed_seconds=time.monotonic() - start, exit_code=1))
            raise
        record['stages'].append(dict(name='build-clock-capacity-table',
            elapsed_seconds=time.monotonic() - start, exit_code=0))
        record['summary'] = {k: table[k] for k in ('part', 'architecture', 'track_capacity_source',
                                                  'half_column_capacity_source', 'table_sha256')}
        record['summary']['clock_regions'] = len(table['regions'])
        record['summary']['half_column_geometry'] = table['half_column_geometry']
        if half_raw is not None:
            record['summary']['half_column_topology'] = table['half_column_topology']
        status = dict(state='completed', exit_code=0)
    except Exception as error:
        status = dict(state='failed', exit_code=1, error=str(error))
        raise
    finally:
        status.update(elapsed_seconds=time.monotonic() - begin,
                      finished=dt.datetime.now().astimezone().isoformat())
        save(directory / 'manifest.json', record)
        save(directory / 'status.json', status)
    print(json.dumps(record['summary'], indent=2))
