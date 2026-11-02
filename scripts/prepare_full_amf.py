#!/usr/bin/env python3
"""Bind a prepared full-design inventory to explicit U250 AMF inputs and physical interfaces."""
import argparse
from collections import defaultdict
import copy
import csv
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from convert_netlist_inventory import convert, digest
from build_physical_boundaries import mapped_y, validate_model_inputs, write_svg
from fabric_x_coordinates import mapped_x
from full_design_metadata import write_async_metadata, SITE_ONLY, FIXED_LEAF


def save(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path, help='Output of prepare_full_design.tcl')
    p.add_argument('--base-config', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path)
    args = p.parse_args()
    source = board = args.directory.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    inventory = board/'inventory'
    if not (inventory/'export_summary.tsv').is_file():
        raise ValueError('Inventory export has not completed')
    shutil.copy2(Path(__file__), out/'adapter_snapshot.py')
    dcp = board/'prepared.dcp'
    config = json.loads(args.base_config.read_text())
    if not math.isfinite(float(config['ClockPeriod'])) or float(config['ClockPeriod']) <= 0 or config.get('ProPrimitiveTiming') != 'false':
        raise ValueError('Explicit positive placement target and ordinary Pro are required')
    default_period = float(config['ClockPeriod'])
    fixed = list(csv.DictReader((board/'fixed_cells.tsv').open(), delimiter='\t'))
    if any(row['primitive'] not in SITE_ONLY | FIXED_LEAF for row in fixed):
        raise ValueError('Unsupported physical interface type')
    base = Path(config['physical boundary model file'])
    report = copy.deepcopy(validate_model_inputs(base, config['vivado extracted device information file']))
    base_report = copy.deepcopy(report)
    mapping = json.loads(Path(report['sources']['coordinates']['path']).read_text())
    fixed_sites_path = board/'fixed_sites.tsv'
    sites = list(csv.DictReader(fixed_sites_path.open(), delimiter='\t'))
    for row in sites:
        row['x'] = mapped_x(float(row['rpm_x']), mapping)
        row['y'] = mapped_y(float(row['rpm_y']), mapping)
        assert int(row['prohibited']) == 0, row
    # Extend only the exterior X edge; preserve all fabric coordinates,
    # capacities, internal boundaries and SLR boundaries.
    xmin, xmax, ymin, ymax = report['coordinate_bounds']
    new_min = min([xmin] + [row['x']-0.25 for row in sites])
    new_max = max([xmax] + [row['x']+0.25 for row in sites])
    for region in report['regions']:
        if region['x0'] == xmin:
            region['x0'] = new_min
        if region['x1'] == xmax:
            region['x1'] = new_max
    report['coordinate_bounds'] = [new_min, new_max, ymin, ymax]
    fabric = Path(config['vivado extracted device information file'])
    with zipfile.ZipFile(fabric) as z:
        original_fabric = z.read('exportSiteLocation')
    names = {line.split()[1] for line in original_fabric.decode().splitlines()}
    added_fabric, added_model = [], []
    existing_sites = {}
    for line in original_fabric.decode().splitlines():
        fields = line.split(); existing_sites[fields[1]] = line
    for row in sites:
        if row['site'] in names:
            line = existing_sites[row['site']]
            for key, value in [('sitetype', row['site_type']), ('slr', row['slr'])]:
                if f'{key}=> {value}' not in line: raise ValueError('Existing fixed site mismatch: '+row['site'])
            continue
        names.add(row['site'])
        regions = [region for region in report['regions'] if region['slr'] == int(row['slr'])
                   and region['x0'] <= row['x'] < region['x1'] and region['y0'] <= row['y'] < region['y1']]
        assert len(regions) == 1, row
        region = regions[0]
        region['site_count'] += 1
        added_fabric.append(f"site=> {row['site']} tile=> {row['tile']} clockRegionName=> {row['clock_region']} "
                            f"sitetype=> {row['site_type']} tiletype=> {row['tile_type']} "
                            f"centerx=> {row['x']:.9f} centery=> {row['y']:.9f} BELs=> [{row['bels']}] "
                            f"slr=> {row['slr']} prohibited=> 0\n")
        added_model.append('\t'.join(map(str, ['SITE', row['site'], row['site_type'],
                                               f"{row['x']:.9f}", f"{row['y']:.9f}", row['slr'], 0, region['id']]))+'\n')
    device = out/'device'
    device.mkdir()
    new_fabric = device/'exportSiteLocation.zip'
    with zipfile.ZipFile(new_fabric, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.writestr('exportSiteLocation', original_fabric+''.join(added_fabric).encode())
    model_dir = device/'model'
    model_dir.mkdir()
    lines = base.read_text().splitlines(keepends=True)
    for i, line in enumerate(lines):
        fields = line.rstrip('\n').split('\t')
        if fields[:2] == ['META', 'fabric_sha256']:
            lines[i] = 'META\tfabric_sha256\t'+digest(new_fabric)+'\n'
        elif fields[:2] == ['META', 'site_count']:
            lines[i] = 'META\tsite_count\t'+str(int(fields[2])+len(added_model))+'\n'
        elif fields[0] == 'REGION':
            region = next(g for g in report['regions'] if g['id'] == int(fields[1]))
            fields[3], fields[4] = str(region['x0']), str(region['x1'])
            lines[i] = '\t'.join(fields)+'\n'
    model = model_dir/'physical_structure.tsv'
    model.write_text(''.join(lines+added_model))
    assert [l for l in base.read_text().splitlines() if l.startswith(('SITE\t','BOUNDARY\t'))] == [
        l for l in ''.join(lines).splitlines() if l.startswith(('SITE\t','BOUNDARY\t'))]
    report['sources']['base_fabric'] = report['sources']['fabric']
    for key, path in [('fabric',new_fabric),('base_model',base),('base_boundaries',base.parent/'boundaries.json'),
                      ('fixed_sites',fixed_sites_path),('full_interface_adapter',out/'adapter_snapshot.py')]:
        report['sources'][key] = {'path':str(path), 'sha256':digest(path)}
    report['fixed_interface_extension'] = {'site_count':len(added_model), 'base_bounds':base_report['coordinate_bounds'],
        'extended_bounds':report['coordinate_bounds'], 'fabric_coordinates_unchanged':True,
        'fabric_capacity_unchanged':True, 'internal_boundaries_unchanged':True,
        'pin_offset_policy':'Fixed interfaces use actual site anchors; composite members are not duplicated in the physical graph.'}
    report['model_sha256'] = digest(model)
    save(model_dir/'boundaries.json', report)
    write_svg(report, model_dir/'physical_regions.svg')
    validate_model_inputs(model, new_fabric)

    fixed = list(csv.DictReader((board/'fixed_cells.tsv').open(), delimiter='\t'))
    fixed_lines = (board/'fixed_units').read_text().splitlines()
    assert fixed_lines[0].startswith('#') and len(fixed_lines)-1 == len(fixed)
    site_types = {row['site']:row['site_type'] for row in sites}
    compatibility = out/'compatibility'
    compatibility.mkdir()
    for key, name in [('cellType2fixedAmo file','cellType2fixedAmo'),('cellType2sharedCellType file','cellType2sharedCellType'),
                      ('sharedCellType2BELtype file','sharedCellType2BELtype')]:
        shutil.copyfile(config[key], compatibility/name)
        config[key] = str(compatibility/name)
    by_type = defaultdict(set)
    for row in fixed:
        assert row['site'] and row['bel'], row
        by_type[row['primitive']].add((site_types[row['site']],row['bel']))
    with (compatibility/'cellType2fixedAmo').open('a') as amounts, (compatibility/'cellType2sharedCellType').open('a') as types, (compatibility/'sharedCellType2BELtype').open('a') as bels:
        # Match the existing conservative one-SLICEM-per-LUTRAM macro policy.
        amounts.write('RAM256X1S 16\n')
        types.write('RAM256X1S SLICEM_LUT\n')
        for kind, mappings in sorted(by_type.items()):
            amounts.write(kind+' 1\n')
            aliases = []
            for site_type, bel in sorted(mappings):
                alias = 'FIXED_'+kind+'_'+site_type+'_'+bel
                aliases.append(alias)
                bels.write(f'{alias} {site_type} {bel}\n')
            types.write(kind+' '+','.join(aliases)+'\n')

    (out/'fixed_occupied_sites.txt').write_text(''.join(name+'\n' for name in sorted(site_types)))
    sync = write_async_metadata(inventory, out)
    netlist = convert(inventory/'cells.tsv', inventory/'nets.tsv', out/'netlist.zip', dcp, inventory/'binding.json')
    cells = {row['cell_id']:row for row in csv.DictReader((inventory/'cells.tsv').open(),delimiter='\t')}
    interfaces = {row['cell_name'] for row in cells.values() if row['primitive'] in SITE_ONLY | FIXED_LEAF}
    declared = {row['cell'] for row in fixed}
    if interfaces != declared or len(declared) != len(fixed):
        raise ValueError('Fixed interface ownership does not cover the canonical inventory')
    owners = {}
    for row in csv.DictReader((board/'fixed_occupancy.tsv').open(), delimiter='\t'):
        if row['cell'] not in declared or row['site'] not in site_types:
            raise ValueError('Unknown composite occupancy owner/site')
        if row['bel'] in owners and owners[row['bel']] != row['cell']:
            raise ValueError('Physical BEL claimed by two canonical cells: '+row['bel'])
        owners[row['bel']] = row['cell']
    preg = {row['cell']:int(row['PREG']) for row in csv.DictReader((inventory/'dsp_preg.tsv').open(),delimiter='\t')}
    used = defaultdict(set)
    csv.field_size_limit(64*1024*1024)
    for row in csv.DictReader((inventory/'nets.tsv').open(),delimiter='\t'):
        if not any(not pin.startswith('PORT:') for pin in row['sinks'].split(',') if pin):
            continue
        ident, pin = row['drivers'].split(':',1)
        if ident in cells and cells[ident]['primitive'] == 'DSP48E2':
            used[cells[ident]['cell_name']].add(pin)
    certified = sorted(name for name,pins in used.items() if preg[name] == 1 and
                       all(re.fullmatch(r'P\[(?:[0-9]|[1-3][0-9]|4[0-7])\]',pin) for pin in pins))
    (out/'dsp_registered_outputs.txt').write_text('AMF_DSP_REGISTERED_OUTPUTS 1\n'+''.join(name+' PREG=1\n' for name in certified))
    save(out/'dsp_certification.json', {'preg':preg,'used_outputs':{n:sorted(p) for n,p in used.items()},'certified_count':len(certified)})
    config.update({'vivado extracted device information file':str(new_fabric), 'physical boundary model file':str(model),
                   'fixed units file':str(board/'fixed_units'),
                   'vivado extracted design information file':str(out/'netlist.zip'), 'clock file':str(out/'netlist.clocks'),
                   'DSP registered outputs file':str(out/'dsp_registered_outputs.txt'),
                   'ASYNC_REG groups file':str(out/'async_groups.txt'),
                   'ASYNC_REG proximity pairs file':str(out/'async_proximity.txt'),
                   'AsyncChainProximityWeight':'4',
                   'fixed occupied sites file':str(out/'fixed_occupied_sites.txt'),
                   'CompositeIOTimingBoundary':'true',
                   'SRL control metadata file':str(inventory/'srl_controls.tsv'),
                   'ClockRegionFabricGeometry':'true',
                   'dumpDirectory':str(out/'unused-inspection-dump')})
    config.pop('BoundaryReportDirectory', None)
    clock_mapping = []
    for row in csv.DictReader((inventory/'clock_net_periods.tsv').open(), delimiter='\t'):
        ident, pin = row['driver'].split(':',1)
        driver = '@PORT/'+pin if ident == 'PORT' else cells[ident]['cell_name']+'/'+pin
        assert driver in netlist['clocks']
        periods = [float(v) for v in row['periods_ns'].split(',') if v]
        row['amf_driver'] = driver
        if periods:
            assert all(value > 0 for value in periods)
            config.setdefault('ClockPeriod:'+driver, str(min(periods)))
            row['selected_period_ns'] = float(config['ClockPeriod:'+driver])
        else:
            row['selected_period_ns'] = default_period
            row['note'] = 'No propagated STA clock on this clock-capable connection; AMF uses the explicit user default.'
        clock_mapping.append(row)
    save(out/'clock_mapping.json',clock_mapping)
    # The fabric extension changes the archive identity, not nominal clock capacity.
    if config.get('clock resource capacity file'):
        capacity = Path(config['clock resource capacity file'])
        lines = capacity.read_text().splitlines()
        changed = 0
        for i, line in enumerate(lines):
            if line.startswith('META\tdevice_sha256\t'):
                lines[i] = 'META\tdevice_sha256\t'+digest(new_fabric); changed += 1
        if changed != 1: raise ValueError('Invalid clock-capacity binding')
        rebound = device/'clock_capacity.tsv';rebound.write_text('\n'.join(lines)+'\n')
        config['clock resource capacity file'] = str(rebound)
    save(out/'config.json',config)
    provenance = {'scope':'canonical full design; case-independent U250 adapter',
        'input_dcp_sha256':digest(dcp), 'input_directory':str(board),
        'inventory_files':{p.name:digest(p) for p in sorted(inventory.glob('*.tsv'))},
        'constraint_xdc_sha256':digest(board/'constraints.xdc'),
        'base_config':str(args.base_config.resolve()), 'base_config_sha256':digest(args.base_config),
        'amf_default_period_ns':default_period, 'vivado_clocks':'unchanged from prepared DCP',
        'vivado_clock_source':'original DCP constraints; no timing constraint rewriting',
        'clock_mapping':str(out/'clock_mapping.json'), 'fixed_interface_cells':len(fixed),
        'async_cells':sync['async_cells'], 'async_groups':len(sync['groups']),
        'physical_model_extension':report['fixed_interface_extension'],
        'io_policy':'independent digital launch/capture boundaries, per-clock target; estimates are not Vivado slack',
        'binary':str(args.binary) if args.binary else None,
        'binary_sha256':digest(args.binary) if args.binary else None}
    save(out/'input_provenance.json',provenance)
    code = 0
    if args.binary:
        with (out/'inspect.log').open('w') as log:
            result = subprocess.run(['python3',str(ROOT/'scripts/amf3.py'),'inspect','--binary',str(args.binary),
                '--config',str(out/'config.json')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            code = result.returncode
    save(out/'status.json', {'state':'completed' if code == 0 else 'failed','exit_code':code,
                            'netlist':netlist,'input_dcp':str(dcp),'fixed_cells':len(fixed)})
    if code: raise RuntimeError('AMF input inspection failed: '+str(out/'inspect.log'))
    print(out)


if __name__ == '__main__':
    main()
