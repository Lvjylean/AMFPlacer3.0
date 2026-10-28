# Supplemental read-only audit. No apply, route_design, or checkpoint write.
lassign $argv checkpoint helper plan out
file mkdir $out
set_param general.maxThreads 2
open_checkpoint $checkpoint
source $helper
e2_sll::refresh_access_index
source $plan
proc planned_records {items prefix loadsVar segmentsVar} {
 upvar 1 $loadsVar loads $segmentsVar segments
 foreach c $items {
  if {[dict exists $c pin]} {dict set loads [dict get $c pin] $prefix} else {
   set resource [dict get $c resource];dict set segments $resource [dict get $c segment]
   planned_records [dict get $c children] [concat $prefix [list $resource]] loads segments
  }
 }
}
set e2_sll::records {}
foreach record $e2_sll_plan {
 set loads {};set segments {};planned_records [dict get $record tree] {} loads segments
 set driver [dict get $record driver]
 lappend e2_sll::records [dict create net [get_property NAME [e2_sll::driver_net $driver]] driver $driver source_slr [dict get $record source_slr] loads $loads segments $segments]
}
set begin [clock milliseconds]
set rc [catch {e2_sll::audit $out} value options]
set f [open $out/audit-status.tsv w];puts $f "return_code\t$rc";puts $f "audit_seconds\t[expr {([clock milliseconds]-$begin)/1000.0}]";puts $f "message\t$value";close $f
if {$rc} {set f [open $out/error.txt w];puts $f [dict get $options -errorinfo];close $f;exit 1}
puts "READ_ONLY_FULL_SLL_AUDIT_COMPLETE"
exit
