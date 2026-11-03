"""Regression for preserving all ASYNC targets before partial import batches."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_full_backend import predeclare_async_targets


class AsyncPreconstraints(unittest.TestCase):
    def fixture(self, directory):
        groups = directory/'groups.txt'
        # Separate AMF groups may still be linked by Vivado across a clock edge.
        groups.write_text('AMF_ASYNC_GROUPS 1\ng0 ff[0]\ng1 ff[1]\n')
        assignments = {'ff[0]': 'SLICE_X0Y0/AFF2', 'ff[1]': 'SLICE_X2Y3/BFF2'}
        original = 'place_design -unplace\nplace_cell {ff[0] SLICE_X0Y0/AFF2}\n'
        return groups, assignments, original

    def test_constraints_complete_before_first_partial_batch(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            groups, assignments, original = self.fixture(directory)
            original_assignments = dict(assignments)
            script, count = predeclare_async_targets(original, assignments, groups)
            self.assertEqual(count, 2)
            self.assertEqual(assignments, original_assignments)
            self.assertTrue(script.endswith(original.split('\n', 1)[1]))
            (directory/'placement').mkdir(); (directory/'reports').mkdir()
            harness = r'''
array set props {}
proc get_cells args {return [list [lindex $args end]]}
proc get_property {key cell} {
    set name [lindex $cell 0]
    if {$key eq "ASYNC_REG"} {return 1}
    if {$key in {IS_LOC_FIXED IS_BEL_FIXED}} {return 0}
    error "Unexpected property $key"
}
proc place_design args {set ::unplaced 1}
proc set_property {key value cell} {
    if {![info exists ::unplaced]} {error "Constraints before unplace"}
    if {$key eq "LOC" && ![info exists {::props(BEL,ff[1])}]} {
        error "Partial BEL inventory before LOC"
    }
    set ::props($key,[lindex $cell 0]) $value
}
proc place_cell args {
    foreach {name site bel} {ff[0] SLICE_X0Y0 AFF2 ff[1] SLICE_X2Y3 BFF2} {
        if {$::props(LOC,$name) ne $site || $::props(BEL,$name) ne $bel} {
            error "Partial chain imported before targets are known"
        }
    }
    puts VERIFIED
}
'''
            test = directory/'test.tcl'
            test.write_text('set script_path {'+str(directory/'placement')+'}\n'+harness+script)
            result = subprocess.run(['tclsh', str(test)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('VERIFIED', result.stdout)

    def test_empty_inventory_leaves_legacy_import_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); groups, assignments, original = self.fixture(directory)
            groups.write_text('AMF_ASYNC_GROUPS 1\n')
            self.assertEqual(predeclare_async_targets(original, assignments, groups), (original, 0))

    def test_existing_fixed_constraints_and_attributes_are_protected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); groups, assignments, original = self.fixture(directory)
            script, _ = predeclare_async_targets(original, assignments, groups)
            for mode, expected in [('LOC', 'fixed LOC'), ('BEL', 'fixed BEL'),
                                   ('ATTRIBUTE', 'non-ASYNC')]:
                with self.subTest(mode=mode):
                    stub = r'''
proc place_design args {}
proc get_cells args {return [list [lindex $args end]]}
proc get_property {key cell} {
    switch $key {
        ASYNC_REG {return [expr {$::mode ne "ATTRIBUTE"}]}
        IS_LOC_FIXED {return [expr {$::mode eq "LOC"}]}
        IS_BEL_FIXED {return [expr {$::mode eq "BEL"}]}
        LOC {return SLICE_X9Y9}
        BEL {return SLICEL.HFF}
        default {error "Unexpected property"}
    }
}
proc set_property args {error MUTATION_BEFORE_VALIDATION}
'''
                    test = directory/'negative.tcl'
                    test.write_text('set mode '+mode+'\n'+stub+script)
                    result = subprocess.run(['tclsh', str(test)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stderr)
                    self.assertNotIn('MUTATION_BEFORE_VALIDATION', result.stderr)

    def test_missing_or_duplicate_members_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); groups, assignments, original = self.fixture(directory)
            for content in ['AMF_ASYNC_GROUPS 1\ng0 absent\n',
                            'AMF_ASYNC_GROUPS 1\ng0 ff[0]\ng1 ff[0]\n', 'bad schema\n']:
                groups.write_text(content)
                with self.assertRaises(ValueError):
                    predeclare_async_targets(original, assignments, groups)

    def test_invalid_bel_and_missing_unplacement_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); groups, assignments, original = self.fixture(directory)
            with self.assertRaises(ValueError):
                predeclare_async_targets(original.replace('place_design -unplace\n', ''), assignments, groups)
            assignments['ff[0]'] = 'SLICE_X0Y0/A6LUT'
            with self.assertRaises(ValueError):
                predeclare_async_targets(original, assignments, groups)


if __name__ == '__main__':
    unittest.main()
