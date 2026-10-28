set out [file normalize [lindex $argv 0]]
set base [file normalize [lindex $argv 1]]
set scripts [file normalize [lindex $argv 2]]
source [file join $scripts dfx_sll.tcl]
proc main {} {
 global out base scripts dfx
 set_param general.maxThreads 4
 set dfx(output) $out
 set dfx(rps) {{a a pb_a} {b b pb_b}}
 set dfx(worker_a) [file join $out worker-a]
 set dfx(worker_b) [file join $out worker-b]
 set dfx(e2_sll_enabled) 1
 set dfx(e2_sll_workers) 2
 set dfx(vivado) /Projects/Xilinx26/2026.1/Vivado/bin/vivado
 open_checkpoint [file join $base common_static_locked.dcp]
 set cells {}
 foreach c [get_cells -hierarchical -filter {IS_PRIMITIVE}] {
  dict set cells [get_property NAME $c] [list [get_property LOC $c] [get_property BEL $c]]
 }
 set routes {}
 foreach net [get_nets -hierarchical] {
  set objects [get_pips -quiet -of_objects $net]
  if {[llength $objects]} {dict set routes [get_property NAME $net] [get_property NAME $objects]}
 }
 foreach rp {a b} {read_checkpoint -strict -cell $rp [file join $base ${rp}_placed.dcp]}
 update_clock_routing -verbose
 dfx_sll::apply
 if {[llength $::e2_sll::records]!=2} {error "Expected two planned internal cross-die nets"}
 write_checkpoint [file join $out imported_sll.dcp]
 route_design
 dfx_sll::audit
 dict for {name location} $cells {
  set c [get_cells [list $name]]
  if {[list [get_property LOC $c] [get_property BEL $c]] ne $location} {error "Frozen cell changed: $name"}
 }
 set retained 0
 dict for {name pips} $routes {
  set actual {}
  foreach pip [get_property NAME [get_pips -quiet -of_objects [get_nets [list $name]]]] {dict set actual $pip 1}
  foreach pip $pips {
   if {![dict exists $actual $pip]} {error "Frozen route lost pip: $name $pip"}
   incr retained
  }
 }
 report_route_status -file [file join $out route-status.rpt]
 report_timing_summary -delay_type min_max -file [file join $out timing.rpt]
 report_drc -file [file join $out drc.rpt]
 write_checkpoint [file join $out routed.dcp]
 set f [open [file join $out frozen-retention.tsv] w]
 puts $f "cells\tpips\n[dict size $cells]\t$retained";close $f
 close_design
 pr_verify -full_check [file join $base base_routed.dcp] [file join $out routed.dcp]
 set f [open [file join $out pr-verify-passed.txt] w];puts $f full_check_passed;close $f
 puts E2_INCREMENTAL_SMALL_ROUTE_PASS
}
if {[catch {main} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
