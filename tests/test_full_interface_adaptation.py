import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from full_design_metadata import async_groups
from convert_netlist_inventory import convert
import subprocess
from run_full_backend import prepare
import test_full_flow


def ff(name, driver='', **changes):
    row = dict(cell=name, primitive='FDRE', clock_driver='clk/O', clock_inverted='0',
               ce_driver='enable/Q', sr_driver='reset/Q', control_polarities='CE=0,R=0',
               d_driver=driver, dont_touch='1')
    row.update(changes)
    return row


class SynchronizerGrouping(unittest.TestCase):
    def test_direct_chain_grouped_without_lut(self):
        result = async_groups([ff('a'), ff('b', 'a/Q'), ff('c', 'b/Q')])
        self.assertEqual(result['groups'], [['a', 'b', 'c']])

    def test_long_chain_splits_at_slice_capacity(self):
        rows = [ff(f'f{i:02}', f'f{i-1:02}/Q' if i else '') for i in range(28)]
        result = async_groups(rows)
        self.assertEqual([len(g) for g in result['groups']], [16, 12])
        self.assertEqual(result['proximity_edges'], [('f15', 'f16')])

    def test_control_difference_splits_but_keeps_proximity(self):
        result = async_groups([ff('a'), ff('b', 'a/Q', primitive='FDPE', sr_driver='preset/Q')])
        self.assertEqual(len(result['groups']), 2)
        self.assertEqual(result['proximity_edges'], [('a', 'b')])

    def test_distinct_clock_and_polarity_are_not_chains(self):
        for change in [dict(clock_driver='other/O'), dict(clock_inverted='1')]:
            result = async_groups([ff('a'), ff('b', 'a/Q', **change)])
            self.assertEqual(result['proximity_edges'], [])
            self.assertEqual(result['excluded_cross_clock_edges'], [('a', 'b')])

    def test_logic_between_ffs_terminates_direct_chain(self):
        result = async_groups([ff('a'), ff('b', 'lut/O')])
        self.assertEqual(len(result['groups']), 2)
        self.assertEqual(result['direct_same_clock_edges'], 0)

    def test_invalid_input_rejected(self):
        for rows in [[ff('a'), ff('a')], [ff('a', clock_driver='')], [ff('a', primitive='LUT1')]]:
            with self.assertRaises(ValueError): async_groups(rows)


class FullInterfaceBackend(unittest.TestCase):
    def fixture(self, root):
        test_full_flow.FullBackend().fixture(root, 'a[0] SLICE_X0Y0/A6LUT\nb SLICE_X0Y1/H6LUT\n')
        shutil.copy2(Path(__file__).resolve().parents[1]/'scripts/full_interface_contract.tcl', root/'scripts/full_interface_contract.tcl')
        kinds = ['IOBUFE3', 'IOBUFDS', 'IBUFDS', 'OBUFDS_DUAL_BUF', 'MMCME4_ADV', 'PLLE4_ADV',
                 'RXTX_BITSLICE', 'BITSLICE_CONTROL', 'TX_BITSLICE_TRI', 'RIU_OR', 'HPIO_VREF']
        fixed = root/'fixed_units'
        fixed.write_text('# fixed\n'+''.join(f'name=> interface[{i}] loc=> SITE_X0Y{i} bel=> SITE_X0Y{i}/BEL_{i}\n' for i in range(len(kinds))))
        groups = root/'async_groups.txt';groups.write_text('AMF_ASYNC_GROUPS 1\n')
        config = json.loads((root/'config.json').read_text())
        config.update({'fixed units file': str(fixed), 'ASYNC_REG groups file': str(groups), 'CompositeIOTimingBoundary': 'true'})
        (root/'config.json').write_text(json.dumps(config))
        (root/'manifest.json').write_text(json.dumps({'inputs': {key: {'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for key, path in [('fixed units file', fixed), ('ASYNC_REG groups file', groups)]}}))
        with zipfile.ZipFile(root/'netlist.zip', 'w') as z:
            z.writestr('allCellPinNet', 'curCell=> a[0] type=> LUT6\ncurCell=> b type=> SRLC32E\n'+''.join(f'curCell=> interface[{i}] type=> {kind}\n' for i, kind in enumerate(kinds)))
        return kinds

    def test_composite_site_targets_and_leaf_bel_locks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);kinds = self.fixture(root)
            prepare(root, root, root, {})
            script = (root/'placement/import_placement.tcl').read_text()
            for i in range(len(kinds)):
                target = f'SITE_X0Y{i}' + (f'/BEL_{i}' if i >= 4 else '')
                self.assertIn(f'place_cell [list {{interface[{i}]}} {{{target}}}]', script)
                self.assertIn(f'set_property IS_LOC_FIXED true [get_cells {{interface[{i}]}}]', script)
                self.assertEqual(f'set_property IS_BEL_FIXED true [get_cells {{interface[{i}]}}]' in script, i >= 4)
            # Placing a later composite can clear an earlier PHY BEL lock in Vivado.
            last_place = script.rfind('place_cell [list {interface[')
            first_lock = script.find('set_property IS_LOC_FIXED true [get_cells {interface[')
            self.assertGreater(first_lock, last_place)
            self.assertTrue((root/'placement/fixed_contract.tsv').exists())

    def test_changed_group_file_cannot_be_reused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);self.fixture(root)
            (root/'async_groups.txt').write_text('tampered\n')
            with self.assertRaisesRegex(ValueError, 'groups changed'): prepare(root, root, root, {})



class BidirectionalInventory(unittest.TestCase):
    def export(self, root, mixed=False):
        dcp = root/'input.dcp';dcp.write_bytes(b'fixture-bound-design')
        output = root/'inventory'
        script = root/'fixture.tcl'
        exporter = Path(__file__).resolve().parents[1]/'scripts/export_full_inventory.tcl'
        stub = r"""
proc current_design {} {return design}
proc set_param args {}
proc open_checkpoint args {}
proc get_cells args {
    if {[lindex $args end] eq "IS_PRIMITIVE"} {return {io tx rx}}
    return {}
}
proc get_property {key objects} {
    set result {}
    foreach obj $objects {
        switch $key {
            PART {set value xcu250-figd2104-2L-e}
            NAME {set value $obj}
            REF_NAME {set value [expr {$obj eq "io" ? "IOBUFE3" : "FDRE"}]}
            IS_CLOCK {set value 0}
            DIRECTION {
                if {$obj in {PAD io/IO}} {set value INOUT} elseif {$obj in {tx/Q io/O}} {set value OUT} else {set value IN}
            }
            default {error "Unexpected property $key"}
        }
        lappend result $value
    }
    return $result
}
proc get_ports args {return PAD}
proc get_nets args {
    if {[lsearch -exact $args -filter] >= 0} {return {}}
    if {[lsearch -exact $args -hierarchical] >= 0} {return {pad send recv}}
    if {[lsearch -exact $args -of_objects] >= 0} {return pad}
    if {[lsearch -exact $args -segments] >= 0} {return [lindex $args end]}
    return {}
}
proc get_pins args {
    global mixed
    if {[lsearch -exact $args -filter] >= 0} {return {}}
    switch [lindex $args end] {
        pad {if {$mixed} {return {io/IO tx/Q}};return io/IO}
        send {return {tx/Q io/I}}
        recv {return {io/O rx/D}}
    }
    error "Unexpected get_pins $args"
}
proc get_clocks args {return {}}
"""
        stub += '\nproc exec args {return {'+hashlib.sha256(dcp.read_bytes()).hexdigest()+'}}\n'
        stub += f'set mixed {int(mixed)}\nset argc 2\nset argv [list {{{dcp}}} {{{output}}}]\nsource {{{exporter}}}\n'
        script.write_text(stub)
        return subprocess.run(['tclsh', str(script)], capture_output=True, text=True), dcp, output

    def test_pad_side_omitted_but_internal_directions_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);run, dcp, output = self.export(root)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            result = convert(output/'cells.tsv', output/'nets.tsv', root/'netlist.zip', dcp, output/'binding.json')
            with zipfile.ZipFile(root/'netlist.zip') as z:
                text = z.read(z.namelist()[0]).decode()
            self.assertIn('pin=> io/I refpin=> I dir=> IN', text)
            self.assertIn('pin=> io/O refpin=> O dir=> OUT', text)
            self.assertNotIn('pin=> io/IO ', text)
            self.assertIn('pad-side-boundary-preserved', (output/'bidirectional_boundary.tsv').read_text())

    def test_internal_driver_on_bidirectional_pad_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            run, _, _ = self.export(Path(temp), mixed=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn('Unsupported mixed/internal bidirectional network', run.stderr)

if __name__ == '__main__': unittest.main()
