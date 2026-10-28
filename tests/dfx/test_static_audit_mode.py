from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class StaticAuditMode(unittest.TestCase):
    def run_tcl(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            script = path / 'check.tcl'
            script.write_text(
                'source {' + str(ROOT / 'scripts/dfx_common.tcl') + '}\n'
                'set dfx(output) {' + directory + '}\n' + body)
            result = subprocess.run(['tclsh', str(script)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            record = path / 'static_placement_audit.tsv'
            return result.stdout, record.read_text() if record.exists() else ''

    def test_skip_does_not_read_or_recompute_snapshot(self):
        output, record = self.run_tcl('''
set dfx(static_placement_audit) skip
proc amf_dfx::static_snapshot {} {error "must not scan"}
proc amf_dfx::read_dict {path} {error "must not load"}
namespace eval amf_multi {proc static_snapshot {} {error "must not scan multi"}}
amf_dfx::check_static_placement /missing one
amf_dfx::check_static_placement /missing assembly multi
''')
        self.assertEqual(record.count('\tskipped\t'), 2)
        self.assertIn('AMF_DFX_STATIC_AUDIT_SKIPPED', output)

    def test_default_full_check_still_compares(self):
        _, record = self.run_tcl('''
proc amf_dfx::static_snapshot {} {return {cell {LUT6 SLICE_X1Y1 A6LUT}}}
amf_dfx::save_dict $dfx(output)/before [amf_dfx::static_snapshot]
amf_dfx::check_static_placement $dfx(output)/before one
''')
        self.assertIn('one\tpassed\t', record)

    def test_full_check_rejects_movement(self):
        output, record = self.run_tcl('''
proc amf_dfx::static_snapshot {} {return {cell {LUT6 SLICE_X2Y1 A6LUT}}}
amf_dfx::save_dict $dfx(output)/before {cell {LUT6 SLICE_X1Y1 A6LUT}}
if {![catch {amf_dfx::check_static_placement $dfx(output)/before one} msg]} {error "movement accepted"}
puts $msg
''')
        self.assertIn('changed at cell', output)
        self.assertNotIn('\tpassed\t', record)

    def test_invalid_mode_rejected(self):
        output, _ = self.run_tcl('''
set dfx(static_placement_audit) typo
if {![catch {amf_dfx::static_audit_enabled} msg]} {error "mode accepted"}
puts $msg
''')
        self.assertIn('Invalid static_placement_audit', output)

    def test_skip_preserves_other_checks_and_final_verification(self):
        _, _ = self.run_tcl('''
set dfx(static_placement_audit) skip
if {![catch {amf_dfx::assert_same {port IN} {port OUT} interface}]} {error "interface check bypassed"}
''')
        for name in ('dfx_backend.tcl', 'dfx_multi_assemble.tcl'):
            text = (ROOT / 'scripts' / name).read_text()
            self.assertIn('pr_verify -full_check', text)
        prepare = (ROOT / 'scripts/dfx_prepare.tcl').read_text()
        self.assertIn('amf_dfx::resource_tables $out $modeled', prepare)
        self.assertIn('"RM port names/directions"', prepare)


if __name__ == '__main__':
    unittest.main()
