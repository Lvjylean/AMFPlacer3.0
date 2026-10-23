#!/usr/bin/env python3
"""Convert a DCP-bound primitive/pin inventory to the legacy AMF netlist format.

No partition or floorplan input is accepted. Constants become AMF constant nets;
external input ports retain distinct driver identities, including external clocks.
"""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import zipfile


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def convert(cells_path, nets_path, output, dcp, binding, clock_pins=None):
    # A clock/reset net can contain hundreds of thousands of sink endpoints.
    csv.field_size_limit(64 * 1024 * 1024)
    output = Path(output)
    if output.exists():
        raise ValueError('Output already exists')
    expected = json.loads(Path(binding).read_text())['input_sha256']
    actual = digest(dcp)
    if actual != expected:
        raise ValueError('DCP does not match the original inventory binding')
    cells, names, counts = {}, {}, Counter()
    with Path(cells_path).open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            ident, name, kind = row['cell_id'], row['cell_name'], row['primitive']
            if ident in cells or name in names or any(c.isspace() for c in name):
                raise ValueError('Duplicate or unsupported cell name: ' + name)
            names[name] = ident
            cells[ident] = (name, kind, {})
            counts[kind] += 1
    clocks, external_drivers, net_ids = set(), set(), set()
    clock_nets, clock_loads = {}, Counter()
    constants = {'GND': '<const0>', 'VCC': '<const1>'}
    pin_count, net_count = 0, 0

    def endpoint(value):
        ident, pin = value.split(':', 1)
        if not pin or any(c.isspace() for c in pin):
            raise ValueError('Invalid pin: ' + value)
        if ident != 'PORT' and ident not in cells:
            raise ValueError('Unknown cell: ' + ident)
        return ident, pin

    def add_pin(ident, pin, direction, net_name, driver):
        nonlocal pin_count
        if ident == 'PORT' or cells[ident][1] in constants:
            return
        pins = cells[ident][2]
        if pin in pins:
            raise ValueError('Pin belongs to multiple inventory nets: ' + ident + ':' + pin)
        pins[pin] = (direction, net_name, driver)
        pin_count += 1
        if direction == 'IN' and driver in clocks:
            clock_loads[driver] += 1

    with Path(nets_path).open() as f:
        for row in csv.DictReader(f, delimiter='\t'):
            ident = row['net_id']
            if ident in net_ids:
                raise ValueError('Duplicate net ID: ' + ident)
            net_ids.add(ident)
            drivers = [endpoint(p) for p in row['drivers'].split(',') if p]
            sinks = [endpoint(p) for p in row['sinks'].split(',') if p]
            if len(drivers) != 1:
                raise ValueError('Expected exactly one driver: ' + ident)
            dc, dp = drivers[0]
            if dc == 'PORT':
                driver = '@PORT/' + dp
                external_drivers.add(driver)
            elif cells[dc][1] in constants:
                driver = constants[cells[dc][1]]
            else:
                driver = cells[dc][0] + '/' + dp
            if 'CLOCK' in row['type']:
                clocks.add(driver)
                clock_nets[driver] = ident
            for cell, pin in drivers:
                add_pin(cell, pin, 'OUT', ident, driver)
            for cell, pin in sinks:
                add_pin(cell, pin, 'IN', ident, driver)
            net_count += 1
    supplemented = 0
    if clock_pins is not None:
        with Path(clock_pins).open() as f:
            if f.readline().rstrip('\n') != 'dcp_sha256\t' + actual:
                raise ValueError('Clock pins do not match the DCP binding')
            for row in csv.DictReader(f, delimiter='\t'):
                driver = row['driver_pin']
                if driver not in clock_nets:
                    raise ValueError('Supplement contains an unknown clock: ' + driver)
                name, slash, pin = row['pin_name'].rpartition('/')
                if not slash or not pin:
                    raise ValueError('Invalid clock pin name')
                if name not in names:
                    continue  # Hierarchy pins and internal Unisim pins are not logical leaf pins.
                ident = names[name]
                previous = cells[ident][2].get(pin)
                if previous is not None:
                    if previous[0] != 'IN' or previous[2] != driver:
                        raise ValueError('Clock supplement conflicts with inventory: ' + row['pin_name'])
                    continue
                add_pin(ident, pin, 'IN', clock_nets[driver], driver)
                supplemented += 1
    if any(clock_loads[c] == 0 for c in clocks):
        raise ValueError('Clock has no input pins; this inventory is incomplete. Supply DCP-bound --clock-pins.')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        with z.open('allCellPinNet', 'w', force_zip64=True) as f:
            for name, kind, pins in cells.values():
                if kind in constants:
                    continue
                f.write(f'curCell=> {name} type=> {kind}\n'.encode())
                for pin, (direction, net_name, driver) in pins.items():
                    f.write((f'   pin=> {name}/{pin} refpin=> {pin} dir=> {direction} '
                             f'net=> {net_name} drivepin=> {driver}\n').encode())
    output.with_suffix('.clocks').write_text(''.join(c + '\n' for c in sorted(clocks)))
    manifest = dict(schema='amf-netlist-inventory-v1', source_dcp_sha256=actual,
                    cells_sha256=digest(cells_path), nets_sha256=digest(nets_path),
                    output_sha256=digest(output), primitive_counts=dict(counts),
                    amf_cell_count=sum(v for k, v in counts.items() if k not in constants),
                    inventory_net_count=net_count, amf_pin_count=pin_count,
                    external_drivers=sorted(external_drivers), clocks=sorted(clocks),
                    clock_input_pins=dict(clock_loads), supplemented_clock_pins=supplemented,
                    clock_pins_sha256=digest(clock_pins) if clock_pins is not None else None,
                    constant_cells_folded=sum(counts[k] for k in constants),
                    floorplan_imported=False)
    output.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cells', required=True, type=Path)
    p.add_argument('--nets', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--dcp', required=True, type=Path)
    p.add_argument('--binding', required=True, type=Path)
    p.add_argument('--clock-pins', type=Path, help='DCP-bound external clock pin supplement')
    a = p.parse_args()
    print(json.dumps(convert(a.cells, a.nets, a.output, a.dcp, a.binding, a.clock_pins), indent=2))
