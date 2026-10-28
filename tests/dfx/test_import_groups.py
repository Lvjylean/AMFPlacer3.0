"""Test that packing adaptation cannot mutate the static shell."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(shutil.which('tclsh'), 'Tcl interpreter required')
class ImportGroups(unittest.TestCase):
    def probe(self, selection, requested, expected_code, expected_writes):
        script = r'''
source [lindex $argv 0]
set dfx(active_partition) rm
set instance top
set writes {}
set groups [dict create rm/a [dict create HLUTNM pair SOFT_HLUTNM soft] rm/b [dict create HLUTNM {} SOFT_HLUTNM {}] static/c [dict create HLUTNM static_pair SOFT_HLUTNM static_soft]]
proc current_instance {args} {
    global instance
    if {[llength $args] == 0} {set instance {}; return {}}
    set instance [lindex $args 0]
}
# Vivado's no-argument form returns the previous instance while going to top.
rename current_instance instance_set
proc current_instance {args} {
    global instance
    if {[llength $args] == 0} {set old $instance; set instance {}; return $old}
    instance_set {*}$args
}
proc get_cells {args} {
    global selection instance
    if {$instance ne "rm"} {error "Unscoped LUT query"}
    return $selection
}
proc get_property {args} {
    global groups
    lassign [lrange $args end-1 end] prop cell
    if {$prop eq "NAME"} {return $cell}
    return [dict get $groups $cell $prop]
}
proc set_property {prop value cells} {
    global groups writes
    foreach cell $cells {
        lappend writes [list $cell $prop]
        dict set groups $cell $prop $value
    }
}
proc amf_dfx::active {name} {return [string match "rm/*" $name]}
set selection [lindex $argv 2]
set requested [lindex $argv 3]
set rc [catch {amf_dfx::reset_active_lut_groups [lindex $argv 1] $requested} msg]
if {$rc != [lindex $argv 4]} {error "Unexpected result: $rc $msg"}
if {[llength $writes] != [lindex $argv 5]} {error "Wrong mutations: $writes"}
if {[dict get $groups static/c HLUTNM] ne "static_pair" || [dict get $groups static/c SOFT_HLUTNM] ne "static_soft"} {error "Static shell changed"}
if {$instance ne "top"} {error "Instance not restored: $instance"}
puts PASS
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'probe.tcl'; p.write_text(script)
            result=subprocess.run(['tclsh',str(p),str(ROOT/'scripts/dfx_import.tcl'),d,selection,requested,str(expected_code),str(expected_writes)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS',result.stdout)
    def test_active_only(self): self.probe('rm/a rm/b','rm/a x rm/b y',0,2)
    def test_static_selection_rejected_before_mutation(self): self.probe('rm/a static/c','rm/a x static/c z',1,0)
    def test_missing_requested_cell_rejected(self): self.probe('rm/a rm/b','rm/a x',1,0)
    def test_empty_groups(self): self.probe('rm/b','rm/b x',0,0)

if __name__ == '__main__': unittest.main()
