set out [file normalize [lindex $argv 0]]
file mkdir $out
set_param general.maxThreads 2
open_checkpoint [lindex $argv 1]
source [file join [file dirname [info script]] .. sll_routes.tcl]
source [lindex $argv 2]
set e2_sll_plan [lrange $e2_sll_plan 0 2]
set f [open $out/three-nets.tcl w];puts $f [list set e2_sll_plan $e2_sll_plan];close $f
set stacks {};set timings {};set calls 0
proc profile {kind args} {
 global stacks timings calls
 set command [lindex $args 0]
 set tag [lindex $command 0]
 if {$kind eq "enter"} {
  dict lappend stacks $tag [clock microseconds]
 } else {
  set stack [dict get $stacks $tag]
  set elapsed [expr {[clock microseconds]-[lindex $stack end]}]
  dict set stacks $tag [lrange $stack 0 end-1]
  dict incr timings $tag $elapsed
  incr calls
  if {$elapsed>100000} {puts "SLOW_QUERY_US $elapsed [string range $command 0 240]";flush stdout}
 }
}
foreach cmd {get_pins get_cells get_nets get_nodes get_site_pins get_slrs get_sites get_tiles get_property set_property} {
 trace add execution $cmd enter [list profile enter]
 trace add execution $cmd leave [list profile leave]
}
puts "QUERY_PROFILE_START";flush stdout
set start [clock milliseconds]
if {[catch {e2_sll::apply $out/three-nets.tcl $out} msg opts]} {
 puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1
}
set f [open $out/timings.tsv w]
dict for {key value} $timings {puts $f "$key\t$value"}
puts $f "total_ms\t[expr {[clock milliseconds]-$start}]";puts $f "calls\t$calls";close $f
puts "QUERY_PROFILE_DONE $timings";flush stdout
exit 0
