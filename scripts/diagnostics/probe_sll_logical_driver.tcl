set study /Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261027-sll-branch-topology-fixed-route-01
source $study/inputs/directed_segments.tcl
set out [file normalize [lindex $argv 0]]
file mkdir $out
set_param general.maxThreads 2
proc save {path value} {set f [open $path w];puts $f $value;close $f}
set failures 0
foreach mode {logical_driver} {
 set dir $out/$mode;file mkdir $dir
 set rc [catch {
  catch {close_design}
  open_checkpoint $study/inputs/template-timed-unrouted.dcp
  set_property DONT_TOUCH false [get_cells {src_* dst_*}]
  set_property DONT_TOUCH false [get_nets cross_*]
  remove_net [get_nets cross_*]
  create_cell -reference LUT4 lut_driver
  set_property INIT 16'hFFFE [get_cells lut_driver]
  place_cell [get_cells lut_driver] SLICE_X215Y270/H6LUT
  for {set i 0} {$i<4} {incr i} {
   create_net input_$i
   connect_net -net [get_nets input_$i] -objects [get_pins [list [format src_%02d/Q $i] lut_driver/I$i]]
  }
  create_net target
  connect_net -net [get_nets target] -objects [get_pins lut_driver/O]
  foreach i {24 25 26 27 28 29} x {6 56 110 110 143 193} y {450 451 692 693 934 935} {
   place_cell [get_cells dst_$i] SLICE_X${x}Y${y}/AFF
   connect_net -net [get_nets target] -objects [get_pins dst_$i/D]
  }
  set_property IS_LOC_FIXED true [get_cells -hier -filter {IS_PRIMITIVE}]
  set_property IS_BEL_FIXED true [get_cells -hier -filter {IS_PRIMITIVE}]
  create_clock -name core_clk -period 10 [get_ports clk]
  set roots [get_nodes -of_objects [get_site_pins -of_objects [get_pins lut_driver/O]]]
  save $dir/roots.txt $roots
  puts "PROBE_ROOTS $mode $roots"
  set seg12 [directed_segment LAG_LAG_X78Y530/UBUMP9 SLR1 SLR2]
  set seg23 [directed_segment LAG_LAG_X134Y775/UBUMP9 SLR2 SLR3]
  set far [concat [list GAP] $seg23 [list [list GAP [terminal_node dst_28/D]]] [list GAP [terminal_node dst_29/D]]]
  set near [concat [list GAP] $seg12 [list [list GAP [terminal_node dst_26/D]] [list GAP [terminal_node dst_27/D]]] $far]
  set tree [list [list GAP [terminal_node dst_24/D]] [list GAP [terminal_node dst_25/D]]]
  set tree [concat [list LOGICAL_DRIVER] $tree $near]
  save $dir/requested.txt $tree
  set_property FIXED_ROUTE $tree [get_nets target]
  save $dir/normalized.txt [get_property FIXED_ROUTE [get_nets target]]
  route_design
  save $dir/route.txt [get_property ROUTE [get_nets target]]
  save $dir/route-status.txt [get_property ROUTE_STATUS [get_nets target]]
  set f [open $dir/pips.tsv w]
  foreach p [get_pips -of_objects [get_nets target]] {puts $f "[get_nodes -uphill -of_objects $p]\t[get_nodes -downhill -of_objects $p]"};close $f
  set f [open $dir/sinks.tsv w];foreach i {24 25 26 27 28 29} {puts $f "dst_$i/D\t[terminal_node dst_$i/D]"};close $f
  save $dir/sll.txt [get_nodes -of_objects [get_nets target] -filter {IS_SLL}]
  write_checkpoint $dir/routed.dcp
 } msg opts]
 save $dir/status.txt [list code $rc message $msg options $opts]
 puts "PROBE_DONE $mode $rc $msg"
 if {$rc} {incr failures}
}
exit [expr {$failures!=0}]
