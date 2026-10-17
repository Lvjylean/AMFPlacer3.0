"""One-time full acceptance -> sealed native cache; immutable incremental reuse."""
import csv
import json
import math
import re
import zipfile
from pathlib import Path
from run_dfx import sha, save

MODEL_KEYS = ('vivado extracted device information file', 'cellType2fixedAmo file',
              'cellType2sharedCellType file', 'sharedCellType2BELtype file',
              'mergedSharedCellType2sharedCellType', 'physical boundary model file',
              'clock resource capacity file')
POLICY_KEYS = ('device', 'ProPrimitiveTiming', 'DSPCritical', 'SRLMaxPerSLICEM',
              'y2xRatio', 'SLRBoundaryDelayNs', 'clockRegionXNum', 'clockRegionYNum')


def validate_full_capture(job, capability, cfg):
    """Reject an unusable full-cache request before synthesis or placement."""
    if not job.get('native_cache_capture'):
        return
    if capability.get('native_static_cache_schema') != 2:
        raise ValueError('Full native capture requires AMF static-cache schema 2')
    policy = job.get('native_dfx_edge_policy', 'off')
    if policy not in ('off', 'clock-region-slice-dsp-v1'):
        raise ValueError('Unknown native DFX edge policy')
    if policy != 'off' and capability.get('native_dfx_edge_policy_schema') != 1:
        raise ValueError('Binary lacks the requested edge policy')
    identity = job.get('native_static_identity')
    if not isinstance(identity, str) or not identity.strip():
        raise ValueError('Full native capture requires a nonempty static identity')
    if job.get('reuse_prepared_input_run'):
        raise ValueError('Native capture requires the matching ordinary full export; use historical migration for old runs')
    coordinate = Path(job.get('native_coordinate_map', Path(cfg['vivado extracted device information file']).with_suffix('.coordinates.json')))
    if not coordinate.is_file():
        raise ValueError('Full native capture requires its device coordinate map: '+str(coordinate))


def model_identity(cfg):
    return {'files': {k: sha(cfg[k]) for k in MODEL_KEYS if k in cfg},
            'policy': {k: cfg.get(k) for k in POLICY_KEYS}}


def verify_cache(native, job, cfg):
    from parallel_runtime import verify_hashes
    verify_hashes({native['path']: native['sha256'], native['contract']: native['contract_sha256']})
    c = json.loads(Path(native['contract']).read_text())
    if (c.get('schema') != 'amf-native-static-contract-v2' or c.get('state') != 'sealed'
            or c['cache_sha256'] != native['sha256'] or c['static_shell_sha256'] != sha(job['static_checkpoint'])
            or float(c['clock_period_ns']) != float(job['clock_period_ns']) or c['device'] != cfg['device']
            or c['model_identity'] != model_identity(cfg)):
        raise ValueError('Native cache does not match static shell, clock, device or AMF model files/policy')
    policy = job.get('native_dfx_edge_policy', 'off')
    if policy not in ('off', 'clock-region-slice-dsp-v1'):
        raise ValueError('Unknown native DFX edge policy')
    if policy != 'off' and c.get('resource_policy', {}).get('version') != policy:
        raise ValueError('Edge policy requires a resource-certified child cache')
    pin_policy=job.get('native_pin_access_policy','off')
    if pin_policy not in ('off','fixed-static-ff-q-v1'):
        raise ValueError('Unknown native pin-access policy')
    if pin_policy!='off' and c.get('pin_access_policy',{}).get('version')!=pin_policy:
        raise ValueError('Pin filtering requires certified access cache')
    return c


def validate_coordinate_map(path, device):
    """Bind the independent geometry adapter to every modeled fabric tile."""
    m = json.loads(Path(path).read_text())
    if m.get('schema') != 'amf-coordinate-map-v2' or m.get('x_kind') != 'tile-columns':
        raise ValueError('Native endpoints require the frozen tile-column coordinate map')
    columns = {int(c['tile_x']): float(c['base_x']) for c in m['tile_columns']}
    origin, pitch = float(m['tile_y_origin']), float(m['tile_y_pitch'])
    if not math.isfinite(origin) or not math.isfinite(pitch) or pitch <= 0:
        raise ValueError('Invalid tile Y mapping')
    tiles = {}
    pattern = re.compile(r'tile=> (\S+)_X(\d+)Y(\d+).*?tiletype=> (\S+) centerx=> (\S+) centery=> (\S+)')
    with zipfile.ZipFile(device) as z:
        names = [n for n in z.namelist() if not n.endswith('/')]
        if len(names) != 1:
            raise ValueError('Ambiguous device model archive')
        with z.open(names[0]) as f:
            for raw in f:
                p = pattern.search(raw.decode())
                if not p:
                    raise ValueError('Unrecognized device tile coordinate record')
                prefix, tx, ty, typ, x, y = p.groups()
                tx, ty, x, y = int(tx), int(ty), float(x), float(y)
                if tx not in columns or typ not in m['type_offsets']:
                    raise ValueError('Device tile lacks an exact coordinate mapping')
                if abs(x - columns[tx] - m['type_offsets'][typ]) > 1e-7:
                    raise ValueError('Device X coordinates disagree with cache adapter')
                key = (prefix, tx, ty)
                tiles[key] = min(tiles.get(key, y), y)
    if not tiles or any(abs(y - (ty-origin)/pitch) > 1e-7 for (_, tx, ty), y in tiles.items()):
        raise ValueError('Device tile Y anchors disagree with cache adapter')
    return m, {'tile_count': len(tiles), 'coordinate_map_sha256': sha(path),
               'device_model_sha256': sha(device)}


def partition_coordinates(nodes, ports, mapping, output):
    columns = {int(c['tile_x']): float(c['base_x']) for c in mapping['tile_columns']}
    with Path(ports).open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    interface = {(r['module'], r['port']): r['direction'] for r in rows}
    clocks = {(r['module'], r['port']) for r in rows if r['clock'] == '1'}
    grouped = {}
    with Path(nodes).open() as f:
        for r in csv.DictReader(f, delimiter='\t'):
            key = r['module'], r['port']
            if interface.get(key) != r['direction']:
                raise ValueError('Unknown/different assigned partition pin interface')
            group = grouped.setdefault(key, [])
            if r['node'] in group:
                raise ValueError('Duplicate assigned partition location')
            group.append(r['node'])
    if grouped.keys() != interface.keys():
        raise ValueError('Assigned partition location table lacks complete port coverage')
    with Path(output).open('x') as f:
        f.write('module\tport\tnode\tx\ty\n')
        for (module, port), values in sorted(grouped.items()):
            if (module, port) in clocks:
                # A distributed clock legitimately has several partition nodes.
                # Preserve every node in the certified source table, but do not
                # turn a clock distribution tree into a data timing endpoint.
                # Native compose retains its @DFX_CLOCK identity separately.
                continue
            if values == ['-']:
                continue  # Dedicated/reduced pins; fail if AMF actually selects one without a coordinate.
            points = set()
            for node in values:
                p = re.fullmatch(r'[^\s/]+_X(\d+)Y(\d+)/[^\s]+', node)
                if not p or int(p[1]) not in columns:
                    raise ValueError('Partition node has no exact modeled tile coordinate: '+node)
                points.add((columns[int(p[1])], (int(p[2])-mapping['tile_y_origin'])/mapping['tile_y_pitch']))
            if len(points) != 1:
                raise ValueError('One port has partition nodes at distinct coordinates; explicit endpoint policy required: '+module+'/'+port)
            x, y = points.pop()
            f.write(f'{module}\t{port}\t{" ".join(sorted(values))}\t{x:.12g}\t{y:.12g}\n')
    return {'ports': len(grouped),
            'located_ports': sum(v != ['-'] for k, v in grouped.items() if k not in clocks),
            'clock_ports': len(clocks), 'clock_nodes': sum(len(grouped[k]) for k in clocks if grouped[k] != ['-'])}


def verify_export_scripts(previous, current):
    """Require the complete OOC exporter source closure, independent of import."""
    names = ('dfx_native_rm_export.tcl', 'dfx_native_export.tcl', 'dfx_common.tcl',
             'dfx_input_scope.tcl', 'dfx_srl_metadata.tcl')
    for name in names:
        if not previous.get(name) or previous.get(name) != current.get(name):
            raise ValueError('OOC export producer/dependency changed: '+name)
    return {name: previous[name] for name in names}


def seal_full(job, out, binary, cfg, stage, acceptance, common):
    out = Path(out); native = out/'native_cache'; cert = native/'certified'
    if not acceptance.get('implementation_verified') or not acceptance.get('timing_met') or common.get('state') != 'verified_empty_rps_after_reopen':
        raise ValueError('Native cache cannot be sealed before full acceptance and empty static shell validation')
    verified = dict(line.split('\t') for line in (cert/'verification.tsv').read_text().splitlines())
    if verified.get('static_context_connectivity_exact') != '1' or verified.get('interfaces_exact') != '1' or int(verified.get('pins_checked', '0')) <= 0:
        raise ValueError('Missing actual native static-context certification')
    timings = [line.split('\t') for line in (out/'stage_times.tsv').read_text().splitlines()]
    if len([r for r in timings if r[0] == 'native_cache_certification' and r[2] == '0']) != 1:
        raise ValueError('Missing successful native certification execution')
    coordinate = Path(job.get('native_coordinate_map', Path(cfg['vivado extracted device information file']).with_suffix('.coordinates.json')))
    mapping, geometry = validate_coordinate_map(coordinate, cfg['vivado extracted device information file'])
    endpoints = partition_coordinates(cert/'partition_nodes.tsv', cert/'native_ports.tsv', mapping, native/'partition_pins.tsv')
    positions = native/'final_static_positions.tsv'
    with positions.open('x') as f, (out/'routed_retention.tsv').open() as src:
        f.write('cell\tsite\tbel\n')
        for r in csv.DictReader(src, delimiter='\t'):
            if r['region'] == 'static':
                if r['origin'] != 'original':
                    raise ValueError('Full backend changed static topology; regenerate cache from actual netlist')
                f.write(r['cell']+'\t'+r['loc']+'\t'+r['bel']+'\n')
    # C++ verifies exact static cell coverage, corrects LOC/BEL once and refuses re-sealing.
    shell = out/'common_static_locked.dcp'
    config = {'Native cache action': 'seal', 'Native static cache proposal': str(native/'proposed.bin'),
              'Native static cache sealed output': str(native/'sealed.bin'),
              'Native static identity': cfg['Native static identity'], 'device': cfg['device'],
              'ClockPeriod': cfg['ClockPeriod'], 'Native static shell sha256': sha(shell),
              'Native static connectivity verified': '1', 'Native interfaces verified': '1',
              'Native final static positions': str(positions), 'Native partition pins file': str(native/'partition_pins.tsv')}
    resources = cert/'resources.tsv'; regions = cert/'resource_regions.tsv'
    resource_policy = None
    if resources.exists() or regions.exists():
        if not resources.is_file() or not regions.is_file():
            raise ValueError('Incomplete final resource inventory')
        side = job.get('native_dfx_edge_side_policy', 'interior-prohibit')
        if side not in ('any-prohibit', 'interior-prohibit'):
            raise ValueError('Unknown edge side policy')
        config.update({'Native resource inventory file': str(resources),
                       'Native resource geometry file': str(regions),
                       'Native resource inventory sha256': sha(resources),
                       'Native resource geometry sha256': sha(regions),
                       'Native resource side policy': side})
        resource_policy = dict(version='clock-region-slice-dsp-v1', cache_format=3, side_policy=side,
                               inventory_sha256=sha(resources), geometry_sha256=sha(regions))
    elif job.get('native_dfx_edge_policy', 'off') != 'off':
        raise ValueError('Requested edge policy lacks final resource inventory')
    save(native/'seal.json', config)
    stage('native_cache_seal', [str(binary), '--native-cache', str(native/'seal.json')])
    proof = [native/'context-spec.tsv', positions, native/'partition_pins.tsv', cert/'verification.tsv',
             cert/'native_ports.tsv', cert/'native_boundary_pins.tsv', cert/'partition_nodes.tsv', coordinate]
    contract = dict(schema='amf-native-static-contract-v2', state='sealed', identity=config['Native static identity'],
                    device=cfg['device'], clock_period_ns=job['clock_period_ns'],
                    cache_sha256=sha(native/'sealed.bin'), static_shell_sha256=config['Native static shell sha256'],
                    full_reference_sha256=sha(out/'base_routed.dcp'), producer_binary_sha256=sha(binary),
                    proposal_sha256=sha(native/'proposed.bin'), model_identity=model_identity(cfg),
                    geometry=geometry, endpoints=endpoints, verification=verified,
                    evidence_hashes={str(p): sha(p) for p in proof},
                    final_positions_corrected_once=True, old_dynamic_cells_retained=False,
                    other_rm_policy='fixed actual partition-pin endpoints; no old internal cells or internal delays',
                    scope='cached static connectivity/placement and site occupancy; no wire/PIP occupancy or routed boundary delay budgets')
    if resource_policy:
        contract['resource_policy'] = resource_policy
        contract['evidence_hashes'].update({str(resources): sha(resources), str(regions): sha(regions)})
    save(native/'contract.json', contract)
    return contract
