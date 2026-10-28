import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_face_detect_flow import validate_export
from verify_face_detect_flow import NORMALIZE_TCL, request_lists


class PreplacementInput(unittest.TestCase):
    def fixture(self, root):
        for name in ['inputs/exported', 'inputs/baseline', 'reports']:
            (root / name).mkdir(parents=True)
        for directory in ['exported', 'baseline']:
            with zipfile.ZipFile(root / 'inputs' / directory / 'faceDetect_allCellPinNet.zip', 'w') as z:
                z.writestr('netlist', 'curCell=> fixed[0] type=> IBUF\ncurCell=> ram type=> RAM64X1S\n')
        with zipfile.ZipFile(root / 'inputs/baseline/faceDetect_clusters.zip', 'w') as z:
            z.writestr('clusters', 'removed_historical_cell\n')
        base = root / 'inputs/exported'
        (base / 'design_state.tsv').write_text('leaf_cells\t2\nblackboxes\t0\nloc_assigned\t1\nloc_fixed\t1\n')
        (base / 'faceDetect_fixedUnits').write_text('# header\nname=> fixed[0] loc=> IOB_X0Y1 bel=> IOB.INBUF\n')
        (base / 'faceDetect_unpredictableMacros').write_text('name=> ram loc=>  bel=>\n')
        (base / 'faceDetect_clocks').write_text('')

    def test_unplaced_ram_and_unused_historical_clusters_are_valid(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.fixture(root)
            validate_export(root, 'preplacement')
            report = json.loads((root / 'reports/input_validation.json').read_text())
            self.assertEqual(report['fixed_cells_validated'], 1)
            self.assertFalse(report['physical_macro_input_used'])
            self.assertFalse(report['cluster_input_used'])
            self.assertEqual((root / 'inputs/expected_cells.tsv').read_text(), 'fixed[0]\tIBUF\nram\tRAM64X1S\n')

    def test_incomplete_or_malformed_fixed_input_fails(self):
        for bad in ['name=> fixed[0] loc=> IOB_X0Y1 bel=> IOB.INBUF\n',
                    '# header\n', '# header\nname=> fixed[0] loc=> bel=>\n']:
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as work:
                root = Path(work)
                self.fixture(root)
                (root / 'inputs/exported/faceDetect_fixedUnits').write_text(bad)
                with self.assertRaises(RuntimeError):
                    validate_export(root, 'preplacement')

    def test_blackbox_is_not_accepted_as_complete_netlist(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.fixture(root)
            (root / 'inputs/exported/design_state.tsv').write_text('leaf_cells\t2\nblackboxes\t1\nloc_fixed\t1\n')
            with self.assertRaises(RuntimeError):
                validate_export(root, 'preplacement')

    def test_audit_parses_tcl_names_without_evaluating_them(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = 'set result [catch {place_cell {a[0] SLICE_X0Y0/AFF\n{b c} SLICE_X0Y0/BFF\n}}]\nputs "ignored"'
            (root / 'raw').write_text(request_lists(source))
            (root / 'normalize.tcl').write_text(NORMALIZE_TCL)
            result = subprocess.check_output(['tclsh', str(root / 'normalize.tcl'), str(root / 'raw'), str(root / 'out')], text=True)
            self.assertEqual(result.strip(), '2 0')
            self.assertEqual((root / 'out').read_text(), 'a[0]\tSLICE_X0Y0/AFF\nb c\tSLICE_X0Y0/BFF\n')

    def test_audit_resolves_vivado_backslash_name_representation(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            requests = root / 'requests.tsv'
            requests.write_text('x\\.q[0]\tSLICE_X0Y0/AFF\n')
            harness = root / 'mock_vivado.tcl'
            harness.write_text(r'''
set mockCellName {x\\.q[0]}
proc set_param {args} {}
proc open_checkpoint {args} {}
proc close_design {} {}
namespace eval xilinx::designutils {
    proc get_leaf_cells {args} {return [list $::mockCellName]}
}
proc get_cells {args} {
    if {[lindex $args end] eq {x\.q[0]}} {return [list $::mockCellName]}
    error "Unexpected cell query"
}
proc get_property {property object} {
    switch $property {
        NAME {return $::mockCellName}
        LOC {return SLICE_X0Y0}
        BEL {return AFF}
        default {error "Unexpected property"}
    }
}
set script [lindex $argv 2]
set argv [list ignored [lindex $argv 0] [lindex $argv 1]]
source $script
''')
            audit = Path(__file__).resolve().parents[1] / 'scripts/audit_face_detect_placement.tcl'
            subprocess.check_output(['tclsh', str(harness), str(requests), str(root), str(audit)], text=True)
            report = json.loads((root / 'placement_audit.json').read_text())
            self.assertEqual(report['found_cells'], 1)
            self.assertEqual(report['exact_location_matches'], 1)
            self.assertEqual(report['escaped_name_aliases'], 1)


if __name__ == '__main__':
    unittest.main()
