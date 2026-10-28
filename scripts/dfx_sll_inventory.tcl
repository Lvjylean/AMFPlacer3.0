# Read-only inventory of immutable routed nodes and actual RP route footprints.
source [lindex $argv 0]
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
 set owners {};set names [dict keys $candidates]
 for {set i 0} {$i<[llength $names]} {incr i 2048} {
  set ns [get_nodes -quiet [lrange $names $i [expr {$i+2047}]]]
  foreach n [get_nets -quiet -of_objects $ns] {dict set owners [get_property NAME $n] $n}
 }
 set used {}
 if {[dict size $owners]} {
  foreach n [get_property NAME [get_nodes -of_objects [dict values $owners]]] {
   if {[dict exists $candidates $n]} {dict set used $n 1}
  }
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
 puts "E2_INCREMENTAL_INVENTORY nodes=[dict size $used] owners=[dict size $owners]"
 close_design
}
if {[catch {main} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
