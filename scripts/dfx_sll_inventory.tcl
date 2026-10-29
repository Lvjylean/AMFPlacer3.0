# Read-only occupancy inventory limited to resources reachable within one RP.
source [lindex $argv 0]
source [file join [file dirname [info script]] sll_usage.tcl]
source [file join [file dirname [info script]] sll_inventory_query.tcl]
proc main {} {
 global dfx
 set_param general.maxThreads $dfx(vivado_threads)
 open_checkpoint $dfx(static_checkpoint)
 set footprints {}
 foreach row $dfx(rps) {
  lassign $row id rp pb
  set tiles [get_property NAME [get_dfx_footprint -route -of_objects [get_cells $rp]]]
  if {![llength $tiles]} {error "Missing route footprint for $rp"}
  foreach tile $tiles {dict set footprints $id $tile 1}
  set f [open $dfx(output)/${id}-tiles.txt w]
  puts $f [join [lsort -unique $tiles] "\n"];close $f
 }
 set resource_nodes {}
 set f [open $dfx(sll_channel_file) r]
 while {[gets $f line]>=0} {
  if {[string index $line 0] eq "#" || [string match "resource\t*" $line]} {continue}
  set row [split $line \t]
  if {[llength $row]!=13} {error "Malformed SLL channel row"}
  foreach node [split [lindex $row 12] ,] {dict lappend resource_nodes [lindex $row 0] $node}
 }
 close $f
 set candidates [e2_sll_inventory::eligible_candidates $resource_nodes $footprints]
 # Whole-design SLL inventory is reported separately from candidate occupancy.
 e2_sll::usage $dfx(output)
 set used [e2_sll_inventory::used_candidates $candidates]
 set f [open $dfx(output)/used_nodes.txt w]
 foreach n [lsort [dict keys $used]] {puts $f $n};close $f
 puts "E2_INCREMENTAL_INVENTORY nodes=[dict size $used] candidates=[dict size $candidates]"
 close_design
}
if {[catch {main} msg options]} {puts stderr $msg;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
