# Read-only inventory of immutable routed nodes and actual RP route footprints.
source [lindex $argv 0]
source [file join [file dirname [info script]] sll_usage.tcl]
proc main {} {
 global dfx
 set_param general.maxThreads $dfx(vivado_threads)
 open_checkpoint $dfx(static_checkpoint)
 set candidates {}
 set f [open $dfx(sll_channel_file) r]
 while {[gets $f line]>=0} {
  if {[string index $line 0] eq "#" || [string match "resource\t*" $line]} {continue}
  set row [split $line \t]
  if {[llength $row]!=13} {error "Malformed SLL channel row"}
  foreach n [split [lindex $row 12] ,] {dict set candidates $n 1}
 }
 close $f
 # Query routed nodes once. Per-candidate get_nets queries scale with the
 # entire channel table even when the frozen shell is nearly empty.
 e2_sll::usage $dfx(output)
 set used {}
 set nets [get_nets -hierarchical]
 foreach n [get_property NAME [get_nodes -quiet -of_objects $nets]] {
  if {[dict exists $candidates $n]} {dict set used $n 1}
 }
 set f [open $dfx(output)/used_nodes.txt w]
 foreach n [lsort [dict keys $used]] {puts $f $n};close $f
 foreach row $dfx(rps) {
  lassign $row id rp pb
  set tiles [get_property NAME [get_dfx_footprint -route -of_objects [get_cells $rp]]]
  if {![llength $tiles]} {error "Missing route footprint for $rp"}
  set f [open $dfx(output)/[set id]-tiles.txt w]
  puts $f [join [lsort -unique $tiles] "\n"];close $f
 }
 puts "E2_INCREMENTAL_INVENTORY nodes=[dict size $used] nets=[llength $nets]"
 close_design
}
if {[catch {main} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
