#!/usr/bin/env python3
"""Normalize an audited soft floorplan without executing Tcl or opening a DCP."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def prepare(source, netlist, netlist_manifest, output):
    source, netlist, netlist_manifest, output = map(Path, (source, netlist, netlist_manifest, output))
    if output.exists():
        raise ValueError('Refusing to overwrite existing floorplan output')
    manifest = json.loads((source / 'manifest.json').read_text())
    amf = json.loads(netlist_manifest.read_text())
    if manifest['input_sha256'] != amf['source_dcp_sha256']:
        raise ValueError('Floorplan and AMF netlist have different source DCP hashes')
    if digest(netlist) != amf['output_sha256']:
        raise ValueError('AMF netlist archive hash mismatch')
    required = ('membership.tsv', 'regions.tcl', 'soft_region_adapter.tcl', 'core_solution.json', 'edge_padding.json')
    verified = {}
    for name in required:
        actual = digest(source / name)
        if actual != manifest['files'].get(name):
            raise ValueError('Floorplan source hash mismatch: ' + name)
        verified[name] = actual

    # Recognize only this declarative export, never evaluate Tcl input.
    regions = {}
    bounds = {}
    ignored = {
        '# Load only the exact-CR Pblock application procedures bundled with this input.',
        'source [file join [file dirname [info script]] soft_region_adapter.tcl]',
        'set planned_regions [dict create]', 'set planned_bounds [dict create]',
    }
    for line in (source / 'regions.tcl').read_text().splitlines():
        if not line.strip() or line in ignored:
            continue
        m = re.fullmatch(r'dict set planned_(regions|bounds) ([A-Za-z0-9_.-]+) \{([^{}]*)\}', line)
        if not m:
            raise ValueError('Unsupported Tcl statement in region export')
        table = regions if m[1] == 'regions' else bounds
        if m[2] in table:
            raise ValueError('Duplicate region or bounds: ' + m[2])
        table[m[2]] = m[3].split()
    changes = {r['module']: r for r in json.loads((source / 'edge_padding.json').read_text())['changes']}
    core = json.loads((source / 'core_solution.json').read_text())['regions']
    if not regions or set(regions) != set(bounds) or set(regions) != set(changes) or set(regions) != set(core):
        raise ValueError('Region/module coverage mismatch')
    for module, crs in regions.items():
        if not crs or len(set(crs)) != len(crs) or any(not re.fullmatch(r'X\d+Y\d+', cr) for cr in crs):
            raise ValueError('Empty, duplicate or invalid CR set: ' + module)
        if set(crs) != set(changes[module]['after_crs']):
            raise ValueError('Final Tcl and soft-expansion region mismatch: ' + module)
        if set(core[module]['crs']) != set(changes[module]['before_crs']):
            raise ValueError('Core and soft-expansion provenance mismatch: ' + module)
        b = list(map(int, bounds[module]))
        if len(b) != 4 or b != changes[module]['after_bounds']:
            raise ValueError('Bounds mismatch: ' + module)
        xy = [tuple(map(int, re.fullmatch(r'X(\d+)Y(\d+)', cr).groups())) for cr in crs]
        if any(not (b[0] <= x <= b[2] and b[1] <= y <= b[3]) for x, y in xy):
            raise ValueError('CR outside declared bounds: ' + module)
    adapter = (source / 'soft_region_adapter.tcl').read_text()
    if 'set_property IS_SOFT true $pb' not in adapter:
        raise ValueError('Export does not declare soft Pblocks')

    cells = {}
    with zipfile.ZipFile(netlist) as z, z.open('allCellPinNet') as f:
        for line in f:
            if line.startswith(b'curCell=> '):
                name, ref = line.decode().rstrip('\r\n')[len('curCell=> '):].split(' type=> ')
                if name in cells:
                    raise ValueError('Duplicate AMF netlist cell')
                cells[name] = ref
    if len(cells) != amf['amf_cell_count']:
        raise ValueError('AMF manifest cell count mismatch')

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='floorplan-', dir=output.parent) as tmp:
        tmp = Path(tmp)
        counts, constants, seen = {}, 0, set()
        with (source / 'membership.tsv').open() as f, (tmp / 'membership.tsv').open('w') as out:
            reader = csv.DictReader(f, delimiter='\t')
            if reader.fieldnames != ['cell', 'module', 'resource', 'ref']:
                raise ValueError('Unexpected source membership header')
            out.write('cell\tmodule\tref\n')
            for r in reader:
                name, module, ref = r['cell'], r['module'], r['ref']
                if name in seen:
                    raise ValueError('Duplicate membership cell: ' + name)
                seen.add(name)
                counts[module] = counts.get(module, 0) + 1
                if module == 'CONSTANT' and ref in ('GND', 'VCC'):
                    constants += 1
                    continue
                if module not in regions or cells.get(name) != ref:
                    raise ValueError('Unknown cell/module or primitive mismatch: ' + name)
                cells.pop(name)
                out.write(f'{name}\t{module}\t{ref}\n')
        if cells:
            raise ValueError('Incomplete membership coverage: ' + str(len(cells)))
        if counts != manifest['counts'] or manifest['module_count'] != len(regions):
            raise ValueError('Source membership counts mismatch')
        with (tmp / 'regions.tsv').open('w') as out:
            out.write('module\tclock_regions\n')
            for module in sorted(regions):
                out.write(module + '\t' + ' '.join(regions[module]) + '\n')
        record = {
            'schema': 'amf-external-floorplan-v1', 'mode': 'initialization-only',
            'source_directory': str(source.resolve()),
            'source_dcp_sha256': manifest['input_sha256'], 'netlist_sha256': digest(netlist),
            'source_manifest_sha256': digest(source / 'manifest.json'), 'verified_source_files': verified,
            'cells': amf['amf_cell_count'], 'constant_cells_omitted': constants,
            'modules': len(regions), 'module_counts': {k: v for k, v in counts.items() if k != 'CONSTANT'},
            'files': {name: digest(tmp / name) for name in ('membership.tsv', 'regions.tsv')},
            'persistent_region_constraints': False,
        }
        (tmp / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
        output.mkdir(exist_ok=False)
        for name in ('membership.tsv', 'regions.tsv', 'manifest.json'):
            shutil.move(str(tmp / name), output / name)
    return record


def validate_external_inputs(config, binary):
    keys = ('external floorplan membership file', 'external floorplan regions file', 'external floorplan manifest file')
    if not any(config.get(k) for k in keys):
        return
    if not all(config.get(k) for k in keys):
        raise ValueError('External floorplan requires membership, regions and provenance manifest')
    manifest = json.loads(Path(config[keys[2]]).read_text())
    if manifest.get('schema') != 'amf-external-floorplan-v1' or manifest.get('mode') != 'initialization-only':
        raise ValueError('Unsupported external floorplan schema or mode')
    for key, name in zip(keys[:2], ('membership.tsv', 'regions.tsv')):
        if digest(config[key]) != manifest['files'][name]:
            raise ValueError('External floorplan input hash mismatch: ' + name)
    if digest(config['vivado extracted design information file']) != manifest['netlist_sha256']:
        raise ValueError('External floorplan was prepared for another AMF netlist')
    result = subprocess.run([str(binary), '--capabilities'], capture_output=True, text=True, timeout=15)
    try:
        supported = result.returncode == 0 and json.loads(result.stdout).get('external_floorplan_initialization_schema') == 1
    except ValueError:
        supported = False
    if not supported:
        raise ValueError('Binary does not support external floorplan initialization; refusing silent fallback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--netlist', type=Path, required=True)
    parser.add_argument('--netlist-manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.netlist, args.netlist_manifest, args.output), indent=2))


if __name__ == '__main__':
    main()

