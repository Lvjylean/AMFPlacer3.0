from pathlib import Path
from types import SimpleNamespace
import sys
import subprocess
import tempfile
import json
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_vivado_baseline import constraint_options, collect


class NativeConstraints(unittest.TestCase):
    def args(self, **changes):
        values = dict(preserve_input_constraints=True, release_io=False,
                      core_clock='pcie_user_clk', clock_period=8.0)
        values.update(changes)
        return SimpleNamespace(**values)

    def test_mixed_clock_design_validates_only_explicit_core(self):
        result = constraint_options(self.args(), {})
        self.assertEqual(result['core_clock'], 'pcie_user_clk')
        self.assertEqual(result['period_ns'], 8.0)
        self.assertTrue(result['preserve_input_constraints'])

    def test_preserve_mode_rejects_io_release_or_missing_target(self):
        for change in ({'release_io': True}, {'core_clock': None}, {'clock_period': None}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                constraint_options(self.args(**change), {})

    def test_invalid_period_is_rejected(self):
        for value in (0, -1, float('inf'), float('nan')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                constraint_options(self.args(clock_period=value), {})

    def test_existing_uniform_clock_mode_is_preserved(self):
        args = self.args(preserve_input_constraints=False, core_clock=None, clock_period=None)
        result = constraint_options(args, {'clock_count': 25, 'vivado_clock_period_ns': 10})
        self.assertEqual(result['clock_count'], 25)
        self.assertEqual(result['period_ns'], 10)
        self.assertFalse(result['preserve_input_constraints'])

    def test_fixed_location_audit_accepts_only_exact_reference_clock_repair(self):
        source = (Path(__file__).resolve().parents[1] / 'scripts/vivado_baseline.tcl').read_text()
        helpers = source.split('if {[catch {\n    timed open', 1)[0]
        self.assertNotEqual(helpers, source)
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / 'reference_fixed_relocations.tsv').write_bytes(
                b'cell\trequested_site\trequested_bel\tactual_site\tactual_bel\r\n'
                b'clock_buffer\tBUFG_GT_X1Y163\tBUFG_GT\tBUFG_GT_X1Y146\tBUFG_GT\r\n')
            harness = out / 'audit.tcl'
            harness.write_text('set argc 12\nset argv [list in.dcp {' + str(out) + '} part 0 8 0 0 1 core 2024.2 0 0]\n'
                'proc set_param {args} {}\n' + helpers + r'''
proc get_cells {args} {return [lindex $args end]}
proc get_property {key cell} {
    global actual_loc kind
    switch $key {LOC {return $actual_loc} BEL {return "BUFG_GT.BUFG_GT"} REF_NAME {return $kind}}
}
set original_fixed [list [list clock_buffer BUFG_GT_X1Y163 BUFG_GT.BUFG_GT]]
set kind BUFG_GT
set actual_loc BUFG_GT_X1Y163
if {[audit_fixed_locations exact] != 0} {error "Exact placement rejected"}
set actual_loc BUFG_GT_X1Y146
if {[audit_fixed_locations repaired] != 1} {error "Reference repair rejected"}
set actual_loc BUFG_GT_X1Y147
if {![catch {audit_fixed_locations unexpected}]} {error "Unrecorded move accepted"}
set actual_loc BUFG_GT_X1Y146
set kind PCIE40E4
if {![catch {audit_fixed_locations wrong_type}]} {error "Non-clock move accepted"}
close $timeline
''')
            subprocess.run(['tclsh', str(harness)], check=True, capture_output=True, text=True)

    def test_verification_requires_checkpoint_and_applicable_constraint_audit(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp);reports = out / 'reports';reports.mkdir()
            manifest = dict(case='test', reference_run='reference', input_dcp_sha256='hash',
                            config=dict(preserve_input_constraints=False))
            (out / 'manifest.json').write_text(json.dumps(manifest))
            (reports / 'route_status.rpt').write_text('# of routable nets : 1\n# of fully routed nets : 1\n# of nets with routing errors : 0\n')
            (reports / 'drc_counts.json').write_text('{"errors":0,"critical_warnings":0}')
            (reports / 'drc.rpt').write_text('')
            audit = dict(clock_snapshot_unchanged=True, io_standards_unchanged=True,
                         input_fixed_locations_verified=False)
            (reports / 'constraint_audit.json').write_text(json.dumps(audit))
            self.assertFalse(collect(out)['implementation_verified'])
            (reports / 'vivado_routed.dcp').write_bytes(b'test checkpoint')
            self.assertTrue(collect(out)['implementation_verified'])
            manifest['config']['preserve_input_constraints'] = True
            (out / 'manifest.json').write_text(json.dumps(manifest))
            self.assertFalse(collect(out)['implementation_verified'])
            audit['input_fixed_locations_verified'] = True
            (reports / 'constraint_audit.json').write_text(json.dumps(audit))
            self.assertTrue(collect(out)['implementation_verified'])


if __name__ == '__main__':
    unittest.main()
