import subprocess
import os
import tempfile
import unittest
from pathlib import Path

HELPER = Path(__file__).resolve().parent / 'dfx_sll.tcl'
TCLSH = os.environ.get('TCLSH', 'tclsh')

class NativeInterfaces(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        version = subprocess.check_output([TCLSH], input='puts [info patchlevel]\n', text=True).strip()
        if tuple(map(int, version.split('.')[:2])) < (8, 6):
            raise unittest.SkipTest('Vivado-compatible Tcl 8.6 required; run on the EDA server')

    def run_tcl(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.tcl'
            path.write_text('rename source core_source\nproc source {args} {if {[lindex $args 0] eq "-notrace"} {set args [lrange $args 1 end]};uplevel 1 [list core_source {*}$args]}\n'
                            + 'source {' + str(HELPER) + '}\nset work {' + directory + '}\n'
                            + 'proc equal {a b} {if {$a ne $b} {error "Expected <$b>, got <$a>"}}\n' + body)
            result = subprocess.run([TCLSH, str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_boundary_index_and_plan_owners(self):
        self.run_tcl('''
set plans {
 {driver static/Q tree {{pin static/D}}}
 {driver rp/a/Q tree {{pin rp/b/D}}}
 {driver static/Q2 tree {{pin rp/b/D}}}
 {driver rp/a/Q2 tree {{pin static/D}}}
 {driver rp/a/Q3 tree {{pin rp2/b/D}}}
 {driver omitted/Q tree {{pin omitted/local}}}
 {driver rpExtra/a/Q tree {{pin static/D}}}
}
lassign [dfx_sll::partition_full_plan $plans {rp rp2} {omitted/Q 1}] exact native
equal [llength $exact] 3
equal [llength $native] 4
equal [dfx_sll::rp_owner rp/sub/a/D {rp rp/sub}] rp/sub
# No RP means ordinary nets are not silently exempted.
lassign [dfx_sll::partition_full_plan $plans {} {}] exact native
equal [llength $exact] 7
equal $native {}
''')

    def test_native_positions_may_change_but_stub_or_conflict_fails(self):
        self.run_tcl('''
rename e2_sll::driver_net {};proc e2_sll::driver_net {driver} {return N}
proc get_nodes {args} {return ACTUAL}
set status ROUTED;set route {ROOT WIRE SINK}
proc get_property {p x} {
 switch $p {NAME {return $x} ROUTE_STATUS {return $::status} ROUTE {return $::route}}
}
set ::dfx_sll::nativeInterfaces {{driver D net N planned_resources PLANNED}}
dfx_sll::audit_native_interfaces $work
set route {ROOT {STUB DEAD} SINK}
if {![catch {dfx_sll::audit_native_interfaces $work}]} {error "STUB accepted"}
set route {ROOT WIRE SINK};set status CONFLICTS
if {![catch {dfx_sll::audit_native_interfaces $work}]} {error "Conflict accepted"}
''')

    def test_full_dispatch_cleanup_resume_and_scope(self):
        self.run_tcl('''
rename dfx_sll::interface_index {};proc dfx_sll::interface_index {} {return {{rp} {edge/Q 1}}}
rename e2_sll::refresh_access_index {};proc e2_sll::refresh_access_index {} {}
rename e2_sll::driver_net {};proc e2_sll::driver_net {d} {return $d}
proc get_property {p x} {
 switch $p {NAME {return $x} FIXED_ROUTE {return {OLD STUB SLL}} IS_ROUTE_FIXED {return 1} ROUTE {return {OLD STUB SLL}}}
}
set changes {};set unroutes {}
proc set_property {args} {lappend ::changes $args}
proc reset_property {args} {lappend ::changes $args}
proc route_design {args} {lappend ::unroutes $args}
rename e2_sll::apply_parallel {}
proc e2_sll::apply_parallel {plan out workers} {
 source $plan
 equal [llength $e2_sll_plan] 1
 equal [dict get [lindex $e2_sll_plan 0] driver] static/Q
 set ::e2_sll::records {{net exact driver static/Q}}
}
set dfx(output) $work;set dfx(e2_sll_enabled) 1;set dfx(e2_sll_workers) 2;set dfx(vivado) mock
file mkdir $work/placement
set f [open $work/placement/sll-plan.tcl w]
puts $f {set e2_sll_plan {{driver edge/Q tree {{resource S segment {A B S C D} children {{pin rp/r/D}}}}} {driver static/Q tree {{pin static/D}}}}};close $f
dfx_sll::apply_full
equal [llength $changes] 2
equal [lindex [lindex $unroutes 0] end] edge/Q
equal [dict get [dfx_sll::full_audit_scope] all_planned_positions_enforced] 0
set ::dfx_sll::nativeInterfaces {};set ::e2_sll::records {}
dfx_sll::restore_full_audit
equal [llength $::dfx_sll::nativeInterfaces] 1
equal [llength $::e2_sll::records] 1
file delete $work/sll/native-interfaces.tcl
if {![catch {dfx_sll::restore_full_audit}]} {error "Missing fallback metadata accepted"}
''')

    def test_legacy_resume_keeps_original_exact_audit(self):
        self.run_tcl('''
set dfx(output) $work;set dfx(e2_sll_enabled) 1;file mkdir $work/sll
set f [open $work/sll/records.tcl w];puts $f {set ::e2_sll::records {{net old}}};close $f
set ::dfx_sll::nativeInterfaces {{driver stale}}
dfx_sll::restore_full_audit
equal $::dfx_sll::nativeInterfaces {}
equal $::dfx_sll::fullScopeMode legacy-exact
equal $::e2_sll::records {{net old}}
''')

    def test_empty_plan_and_zero_rp_preserve_noop(self):
        self.run_tcl('''
proc get_cells {args} {return {}}
proc get_property {p x} {return $x}
lassign [dfx_sll::interface_index] rps drivers
equal $rps {};equal $drivers {}
set f [open $work/empty.tcl w];puts $f {set e2_sll_plan {}};close $f
set plan [dfx_sll::prepare_full_plan $work/empty.tcl $work/out]
source $plan
equal $e2_sll_plan {}
equal $::dfx_sll::fullNativeCount 0
''')

    def test_disabled_does_not_query_design(self):
        self.run_tcl('''
set dfx(e2_sll_enabled) 0
dfx_sll::apply_full
dfx_sll::restore_full_audit
dfx_sll::apply
''')

    def test_empty_vivado_drs_is_not_a_constraint(self):
        self.run_tcl('''
foreach route [list {} {{}} {{{ }}}] {equal [dfx_sll::route_has_content $route] 0}
equal [dfx_sll::route_has_content {ROOT GAP {SLL SINK}}] 1
''')

if __name__ == '__main__':
    unittest.main()
