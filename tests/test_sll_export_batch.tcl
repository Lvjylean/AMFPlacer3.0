source [file join [file dirname [info script]] .. scripts sll_routes.tcl]
proc get_nets {names} {return $names}
proc reset_property {property nets} {lappend ::events [list reset $nets]}
proc route_design {args} {
 set nets [lindex $args end]
 if {[lsearch -exact $args -unroute]>=0} {lappend ::events [list unroute $nets];return}
 lappend ::events [list route $nets]
 if {$::failRoute} {error "injected route failure"}
}
rename e2_sll::native_crossing_source e2_sll::native_crossing_source_original
proc e2_sll::native_crossing_source {net roots} {
 lappend ::events [list read $net $roots]
 if {$::failSelection eq $net} {return {}}
 return [lindex $roots end]
}
set requests [list [list {net[0]} {T/COUT GAP SLL/A {GAP PIN/A}} {T/COUT T/HMUX}] [list net1 {GAP SLL/B PIN/B} {U/COUT U/HMUX}]]
set events {};set failRoute 0;set failSelection {}
set result [e2_sll::native_source_routes $requests]
if {[dict get $result {net[0]}] ne {T/HMUX GAP SLL/A {GAP PIN/A}}} {error "first tree changed"}
if {[dict get $result net1] ne {U/HMUX GAP SLL/B PIN/B}} {error "second tree changed"}
set actions [lmap event $events {lindex $event 0}]
if {$actions ne {reset unroute route read read unroute}} {error "not one native route batch: $events"}
foreach index {0 1 2 5} {if {[lindex $events $index 1] ne {{net[0]} net1}} {error "incomplete batch"}}
puts "PASS one_batch_preserves_per_net_trees"
foreach failure {route selection} {
 set events {};set failRoute [expr {$failure eq "route"}];set failSelection [expr {$failure eq "selection" ? "net1" : ""}]
 if {![catch {e2_sll::native_source_routes $requests} message]} {error "failure swallowed"}
 if {[lindex $events end] ne {unroute {{net[0]} net1}}} {error "probe routes leaked: $events"}
 puts "PASS ${failure}_failure_cleanup"
}
set failRoute 0;set failSelection {};set events {}
if {[e2_sll::native_source_routes {}] ne {} || [llength $events]} {error "empty batch mutated routing"}
if {![catch {e2_sll::native_source_routes [concat $requests [list [lindex $requests 0]]]} message] || [llength $events]} {error "duplicate was not rejected before mutation"}
puts "PASS empty_and_duplicate_batches"

# The production entry point must keep audit-compatible shard files while
# avoiding checkpoint/worker overhead for a small plan.
source [file join [file dirname [info script]] .. scripts sll_parallel.tcl]
rename source source_original
proc source {args} {uplevel 1 [list source_original [lindex $args end]]}
rename e2_sll::apply e2_sll::apply_original
proc e2_sll::apply {plan out} {
 source $plan
 file mkdir $out
 set ::e2_sll::records $e2_sll_plan
 set ::e2_sll::exportLocks [dict create cell {SITE BEL {I0:A1}}]
 set f [open $out/sll-fixed-route.xdc w];puts $f "# test constraint";close $f
}
proc write_checkpoint {args} {error "small plan must not write a checkpoint"}
set dir [file normalize [lindex $argv 0]]
if {[file exists $dir]} {error "test directory must be new"}
file mkdir $dir
set plan [list [dict create driver source/Q tree [list [dict create pin receiver/D slr 1]]]]
set f [open $dir/plan.tcl w];puts $f [list set e2_sll_plan $plan];close $f
e2_sll::apply_parallel $dir/plan.tcl $dir/output 4
source $dir/output/sll-parallel/shard-00/apply/records.tcl
if {$::e2_sll_worker_records ne $plan} {error "audit records changed"}
source $dir/output/sll-parallel/shard-00/apply/locks.tcl
if {![dict exists $::e2_sll_worker_locks cell]} {error "endpoint locks lost"}
if {![file exists $dir/output/sll-fixed-route.xdc]} {error "constraint export missing"}
if {![catch {e2_sll::apply_parallel $dir/plan.tcl $dir/output 4} message]} {error "repeated output overwritten"}
puts "PASS small_plan_in_session_and_audit_handoff"
