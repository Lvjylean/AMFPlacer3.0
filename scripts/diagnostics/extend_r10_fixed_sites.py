#!/usr/bin/env python3
"""Append fixed I/O/BUFG sites without changing R10 fabric geometry or capacity."""
import argparse
import copy
import csv
import json
from pathlib import Path
import shutil
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_physical_boundaries import digest, mapped_y, validate_model_inputs
from fabric_x_coordinates import mapped_x


def extend(base_model, fixed_sites, output):
    base_model, fixed_sites, output = map(Path, (base_model, fixed_sites, output))
    original = json.loads((base_model.parent / 'boundaries.json').read_text())
    fabric = Path(original['sources']['fabric']['path'])
    validate_model_inputs(base_model, fabric)
    if output.exists():
        raise ValueError('Refusing to overwrite derived model inputs')
    mapping = json.loads(Path(original['sources']['coordinates']['path']).read_text())
    with zipfile.ZipFile(fabric) as z:
        old_fabric = z.read('exportSiteLocation')
    existing = {line.split()[1] for line in old_fabric.decode().splitlines()}
    report = copy.deepcopy(original)
    added_fabric, added_model = [], []
    types = set()
    with fixed_sites.open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    for r in rows:
        if r['site'] in existing:
            raise ValueError('Duplicate fixed site: ' + r['site'])
        existing.add(r['site'])
        if r['site_type'] not in ('HPIOB', 'HPIOB_M', 'HPIOB_S', 'HPIOB_SNGL', 'BUFGCE'):
            raise ValueError('Unreviewed fixed site type: ' + r['site_type'])
        types.add(r['site_type'])
        x = mapped_x(float(r['rpm_x']), mapping)
        y = mapped_y(float(r['rpm_y']), mapping)
        matches = [g for g in report['regions'] if g['slr'] == int(r['slr'])
                   and g['x0'] <= x < g['x1'] and g['y0'] <= y < g['y1']]
        if len(matches) != 1:
            raise ValueError('Fixed site outside R10 regions: ' + repr((r['site'], x, y)))
        region = matches[0]
        region['site_count'] += 1
        region['prohibited_count'] += int(r['prohibited'])
        added_fabric.append(f"site=> {r['site']} tile=> {r['tile']} clockRegionName=> {r['clock_region']} "
                            f"sitetype=> {r['site_type']} tiletype=> {r['tile_type']} "
                            f"centerx=> {x:.9f} centery=> {y:.9f} BELs=> [{r['bels']}] "
                            f"slr=> {r['slr']} prohibited=> {r['prohibited']}\n")
        added_model.append('\t'.join(map(str, ['SITE', r['site'], r['site_type'],
                                               f'{x:.9f}', f'{y:.9f}', r['slr'],
                                               r['prohibited'], region['id']])) + '\n')
    output.mkdir(parents=True)
    derived_fabric = output / 'exportSiteLocation.zip'
    with zipfile.ZipFile(derived_fabric, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.writestr('exportSiteLocation', old_fabric + ''.join(added_fabric).encode())
    # The original REGION/BOUNDARY and fabric SITE records remain byte-identical.
    lines = base_model.read_text().splitlines(keepends=True)
    base_count = None
    for i, line in enumerate(lines):
        if line.startswith('META\tfabric_sha256\t'):
            lines[i] = 'META\tfabric_sha256\t' + digest(derived_fabric) + '\n'
        elif line.startswith('META\tsite_count\t'):
            base_count = int(line.split()[2])
            lines[i] = 'META\tsite_count\t' + str(base_count + len(rows)) + '\n'
    if base_count is None:
        raise ValueError('Missing base site count')
    model_dir = output / 'model'
    model_dir.mkdir()
    model = model_dir / 'physical_structure.tsv'
    model.write_text(''.join(lines + added_model))
    report['sources']['base_fabric'] = original['sources']['fabric']
    for key, path in [('fabric', derived_fabric), ('base_model', base_model),
                      ('base_boundaries', base_model.parent / 'boundaries.json'),
                      ('fixed_sites', fixed_sites), ('fixed_site_adapter', Path(__file__))]:
        report['sources'][key] = {'path': str(path.resolve()), 'sha256': digest(path)}
    report['fixed_site_extension'] = dict(count=len(rows), site_types=sorted(types),
        capacity_added=0, fabric_records_unchanged=True, region_geometry_unchanged=True,
        boundary_penalties_unchanged=True, coordinate_method='R10 RPM anchor interpolation')
    report['model_sha256'] = digest(model)
    (model_dir / 'boundaries.json').write_text(json.dumps(report, indent=2) + '\n')
    shutil.copy2(base_model.parent / 'physical_regions.svg', model_dir / 'physical_regions.svg')
    validate_model_inputs(model, derived_fabric)
    print(json.dumps(report['fixed_site_extension'], indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base-model', type=Path, required=True)
    p.add_argument('--fixed-sites', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    extend(a.base_model, a.fixed_sites, a.output)
