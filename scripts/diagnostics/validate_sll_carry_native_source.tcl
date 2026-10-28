lassign $argv out input helper oldRun namesFile
file mkdir $out
set_param general.maxThreads 2
open_checkpoint $input
source $helper
source $namesFile
set drivers {}
foreach f [glob $oldRun/reports/sll-parallel/shard-*/apply/records.tcl] {
 source $f
 foreach r $e2_sll_worker_records {
  if {[dict get $r net] in $failedNets} {dict set drivers [dict get $r driver] 1}
 }
}
source $oldRun/placement/sll-plan.tcl
set selected {}
foreach r $e2_sll_plan {if {[dict exists $drivers [dict get $r driver]]} {lappend selected $r}}
if {[llength $selected]!=2} {error "Expected two carry test nets"}
set f [open $out/plan.tcl w];puts $f [list set e2_sll_plan $selected];close $f
e2_sll::apply $out/plan.tcl $out
set nets {}
foreach r $e2_sll::records {
 if {[dict get $r source_mode] ne "vivado_native_router_source"} {error "Expected native source resolution"}
 lappend nets [get_nets [dict get $r net]]
}
route_design -nets $nets
e2_sll::audit $out
report_route_status -of_objects $nets -file $out/route-status.rpt
puts "E2_CARRY_NATIVE_EXPORT_VALIDATED"
exit 0
