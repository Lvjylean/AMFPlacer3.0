# Compare constraint generation on an identical placed checkpoint and plan.
lassign $argv checkpoint plan helper out
if {[file exists $out]} {error "Refusing to overwrite benchmark output $out"}
file mkdir $out
set_param general.maxThreads 1
set started [clock microseconds]
set profile {};set profileCounts {}
proc save {path value} {set f [open $path w];puts $f $value;close $f}
if {[catch {
 open_checkpoint $checkpoint
 set openSeconds [expr {([clock microseconds]-$started)/1e6}]
 source $helper
 set ::e2_sll::allowDfxRouting 1
 foreach command {refresh_access_index pin_object physical_pins terminal validate_endpoint children native_source_route native_source_routes needs_native_source_resolution} {
  if {![llength [info commands ::e2_sll::$command]]} {continue}
  rename ::e2_sll::$command ::e2_sll::profile_original_$command
  proc ::e2_sll::$command {args} [string map [list @NAME@ $command] {
   set started [clock microseconds]
   try {return [uplevel 1 [list ::e2_sll::profile_original_@NAME@ {*}$args]]} finally {
    dict incr ::profile @NAME@ [expr {[clock microseconds]-$started}]
    dict incr ::profileCounts @NAME@
   }
  }]
 }
 set started [clock microseconds]
 e2_sll::apply $plan $out
 set applySeconds [expr {([clock microseconds]-$started)/1e6}]
 save $out/records.tcl [list set benchmark_records $::e2_sll::records]
 save $out/locks.tcl [list set benchmark_locks $::e2_sll::exportLocks]
 set normalized {}
 foreach record $::e2_sll::records {
  set name [dict get $record net]
  dict set normalized $name [get_property FIXED_ROUTE [get_nets [list $name]]]
 }
 save $out/normalized.tcl [list set benchmark_routes $normalized]
 set f [open $out/timings.tsv w];puts $f "phase\tseconds\tcalls"
 puts $f "open_checkpoint\t$openSeconds\t1";puts $f "apply\t$applySeconds\t1"
 dict for {phase micros} $profile {puts $f "$phase\t[expr {$micros/1e6}]\t[dict get $profileCounts $phase]"};close $f
 save $out/tools.txt [list vivado [version -short] part [get_property PART [current_design]] clocks [get_property PERIOD [get_clocks]]]
 save $out/status.txt completed
} failure options]} {
 save $out/status.txt [list failed $failure $options]
 puts stderr [dict get $options -errorinfo];exit 1
}
puts "SLL_BENCHMARK_COMPLETED apply_seconds=$applySeconds"
exit 0
