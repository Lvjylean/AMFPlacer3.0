import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from run_full_backend import preserve_lut62_pairing

LEGACY = r'''foreach amf_prop {HLUTNM SOFT_HLUTNM} {
  set amf_grouped [get_cells -quiet -hierarchical -filter "$amf_prop != \"\""]
  if {[llength $amf_grouped]} {set_property $amf_prop {} $amf_grouped}
}'''

MOCK = r'''
set cells [dict create]
proc add {name kind hlut bel} {
    dict set ::cells $name [dict create NAME $name REF_NAME $kind HLUTNM $hlut LUTNM {} SOFT_HLUTNM {} bels $bel]
}
add block/dual LUT6_2 {} {SLICE_X1Y2/A6LUT}
add block/dual/LUT5 LUT5 custom_pair {SLICE_X1Y2/A5LUT}
add block/dual/LUT6 LUT6 custom_pair {SLICE_X1Y2/A6LUT}
add ordinary LUT5 LUT6_2 {SLICE_X2Y2/B5LUT}
dict set cells ordinary SOFT_HLUTNM another_hint
proc get_cells {args} {
    set index [lsearch -exact $args -filter]
    if {$index >= 0} {
        set filter [lindex $args [expr {$index+1}]]
        set result [list]
        dict for {name props} $::cells {
            if {$filter eq {REF_NAME == LUT6_2}} {
                if {[dict get $props REF_NAME] eq "LUT6_2"} {lappend result $name}
            } else {
                set prop [lindex $filter 0]
                if {[dict get $props $prop] ne ""} {lappend result $name}
            }
        }
        return $result
    }
    set name [lindex $args end]
    if {[dict exists $::cells $name]} {return [list $name]}
    return {}
}
proc get_property {prop objects} {return [dict get $::cells [lindex $objects 0] $prop]}
proc set_property {prop value objects} {
    foreach name $objects {dict set ::cells $name $prop $value}
}
proc get_bels {args} {return [dict get $::cells [lindex $args end] bels]}
proc assert {condition message} {if {![uplevel 1 [list expr $condition]]} {error $message}}
'''


class Lut62PairingTests(unittest.TestCase):
    def run_tcl(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            script = path / 'run.tcl'
            script.write_text('set out {' + directory + '}\n' + MOCK + '\nsource {' +
                              str(ROOT/'scripts/lut62_pairing.tcl') + '}\n' + body)
            result = subprocess.run(['tclsh', str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return {f.stem: json.loads(f.read_text()) for f in path.glob('*.json')}

    def test_replace_only_cleanup_preserves_placement_bytes(self):
        prefix = 'place_cell {block/dual SLICE_X1Y2/A6LUT}\n'
        suffix = '\nplace_cell {ordinary SLICE_X2Y2/B5LUT}\n'
        result = preserve_lut62_pairing(prefix + LEGACY + suffix)
        self.assertTrue(result.startswith(prefix))
        self.assertTrue(result.endswith(suffix))
        self.assertNotIn(LEGACY, result)
        self.assertEqual(result.count('amf_lut62::capture'), 1)

    def test_reject_unknown_or_duplicate_boundaries(self):
        for script in ('set_property HLUTNM {} [get_cells -hierarchical *]', LEGACY + '\n' + LEGACY):
            with self.assertRaises(ValueError):
                preserve_lut62_pairing(script)

    def test_export_without_group_operations_is_unchanged(self):
        script = 'place_cell {plain SLICE_X1Y2/A6LUT}\n'
        self.assertEqual(preserve_lut62_pairing(script), script)

    def test_preserve_by_parent_ownership_not_group_spelling(self):
        reports = self.run_tcl('''
amf_lut62::capture
amf_lut62::clear_ordinary_groups
assert {[dict get $cells block/dual/LUT5 HLUTNM] eq "custom_pair"} "lost LUT5"
assert {[dict get $cells block/dual/LUT6 HLUTNM] eq "custom_pair"} "lost LUT6"
assert {[dict get $cells ordinary HLUTNM] eq ""} "ordinary group retained"
assert {[dict get $cells ordinary SOFT_HLUTNM] eq ""} "ordinary soft group retained"
amf_lut62::check placed 1
''')
        self.assertEqual(reports['placed_lut62']['physical_pairs'], 1)
        self.assertEqual(reports['lut62_group_cleanup']['protected_members'], 2)
        self.assertEqual(reports['lut62_group_cleanup']['cleared_ordinary_properties'], 2)

    def test_missing_member_rejected_before_clear(self):
        self.run_tcl('''
dict unset cells block/dual/LUT5
assert {[catch {amf_lut62::capture} message]} "missing child accepted"
assert {[dict get $cells ordinary HLUTNM] eq "LUT6_2"} "cleared before validation"
''')

    def test_original_pair_must_be_valid(self):
        self.run_tcl('''
dict set cells block/dual/LUT5 HLUTNM different
assert {[catch {amf_lut62::capture} message]} "invalid original pair accepted"
''')

    def test_detect_pairing_tampering(self):
        reports = self.run_tcl('''
amf_lut62::capture
dict set cells block/dual/LUT5 HLUTNM {}
assert {[catch {amf_lut62::check imported 0} message]} "pair loss accepted"
''')
        self.assertEqual(reports['imported_lut62']['pairing_changes'], 1)

    def test_detect_same_bel_despite_correct_parent_location(self):
        reports = self.run_tcl('''
amf_lut62::capture
dict set cells block/dual/LUT5 bels {SLICE_X1Y2/A6LUT}
assert {[catch {amf_lut62::check placed 1} message]} "internal BEL collision accepted"
''')
        self.assertEqual(reports['placed_lut62']['physical_pairs'], 0)

    def test_default_repair_can_defer_unplaced_pair(self):
        reports = self.run_tcl('''
amf_lut62::capture
dict set cells block/dual/LUT5 bels {}
dict set cells block/dual/LUT6 bels {}
amf_lut62::check imported 0
assert {[catch {amf_lut62::check placed 1} message]} "unplaced final pair accepted"
''')
        self.assertEqual(reports['imported_lut62']['physical_pairs'], 0)

    def test_no_lut62_design(self):
        reports = self.run_tcl('''
foreach name {block/dual block/dual/LUT5 block/dual/LUT6} {dict unset cells $name}
amf_lut62::capture
amf_lut62::clear_ordinary_groups
amf_lut62::check placed 1
''')
        self.assertEqual(reports['placed_lut62']['pairs'], 0)


if __name__ == '__main__':
    unittest.main()
