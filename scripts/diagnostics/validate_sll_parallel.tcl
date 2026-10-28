set study /Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261027-sll-branch-topology-fixed-route-01
source $study/inputs/directed_segments.tcl
set out [file normalize [lindex $argv 0]]
file mkdir $out
set_param general.maxThreads 2
proc save {path value} {set f [open $path w];puts $f $value;close $f}
source [file join [file dirname [info script]] .. sll_routes.tcl]
source [file join [file dirname [info script]] .. sll_parallel.tcl]
set failures 0
set modes [lrange $argv 1 end]
if {![llength $modes]} {set modes {lut_local_near_far lut_remote_only ff_lower_local_upper lut_lower_local_upper lut_two_remote_groups}}
foreach mode $modes {
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
  set_property DONT_TOUCH true [get_cells lut_driver]
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
  if {$mode eq "lut_internal_receiver"} {
   place_cell [get_cells dst_24] SLICE_X215Y270/HFF
  }
  if {$mode eq "lut_remote_only"} {
   foreach i {24 25} y {690 691} {place_cell [get_cells dst_$i] SLICE_X56Y${y}/AFF}
  }
  if {$mode in {ff_lower_local_upper lut_lower_local_upper}} {
   if {$mode eq "ff_lower_local_upper"} {
    disconnect_net -net [get_nets target] -objects [get_pins lut_driver/O]
    place_cell [get_cells src_24] SLICE_X215Y270/AFF
    connect_net -net [get_nets target] -objects [get_pins src_24/Q]
   }
   foreach i {24 25 26 27 28 29} x {6 56 110 110 143 193} y {210 211 452 453 694 695} {
    place_cell [get_cells dst_$i] SLICE_X${x}Y${y}/AFF
   }
  }
  if {$mode eq "lut_two_remote_groups"} {
   foreach i {28 29} x {143 193} y {694 695} {place_cell [get_cells dst_$i] SLICE_X${x}Y${y}/AFF}
  }
  if {$mode eq "lut_shared_control_access"} {
   set oneNet [get_nets -of_objects [get_pins src_00/CE]]
   foreach i {24 25 26 27 28 29} x {56 56 110 110 143 143} y {450 450 692 692 934 934} bel {AFF BFF AFF BFF AFF BFF} {
    disconnect_net -net [get_nets target] -objects [get_pins dst_$i/D]
    disconnect_net -net [get_nets -of_objects [get_pins dst_$i/CE]] -objects [get_pins dst_$i/CE]
    connect_net -net $oneNet -objects [get_pins dst_$i/D]
    connect_net -net [get_nets target] -objects [get_pins dst_$i/CE]
    place_cell [get_cells dst_$i] SLICE_X${x}Y${y}/$bel
    set_property DONT_TOUCH true [get_cells dst_$i]
   }
  }
  if {$mode eq "lut_bram_multi_access"} {
   create_cell -reference RAMB36E2 bram_receiver
   set_property -dict {READ_WIDTH_A 36 READ_WIDTH_B 36 WRITE_WIDTH_A 36 WRITE_WIDTH_B 36 DOA_REG 0 DOB_REG 0} [get_cells bram_receiver]
   set sites [get_sites -of_objects [get_slrs SLR2] -filter {NAME =~ RAMB36*}]
   if {![llength $sites]} {error "No RAMB36 site in SLR2"}
   place_cell [get_cells bram_receiver] [lindex $sites 0]
   set_property DONT_TOUCH true [get_cells bram_receiver]
   set clkNet [get_nets -of_objects [get_pins src_00/C]]
   set zeroNet [get_nets -of_objects [get_pins src_00/R]]
   set oneNet [get_nets -of_objects [get_pins src_00/CE]]
   foreach p [get_pins -of_objects [get_cells bram_receiver] -filter {DIRECTION == IN}] {
    set tail [file tail $p]
    if {$tail eq "ADDRBWRADDR\[6\]"} {set net [get_nets target]} elseif {$tail in {CLKARDCLK CLKBWRCLK}} {set net $clkNet} elseif {$tail in {ENARDEN ENBWREN ADDRENA ADDRENB REGCEAREGCE REGCEB}} {set net $oneNet} else {set net $zeroNet}
    connect_net -net $net -objects $p
   }
   set nodes [get_nodes -of_objects [get_site_pins -of_objects [get_pins {bram_receiver/ADDRBWRADDR[6]}]]]
   save $dir/bram-input-nodes.txt $nodes
   if {[llength $nodes]<2} {error "BRAM multi-access receiver was not exercised: $nodes"}
  }
  if {$mode in {lut_dsp_receiver dsp_driver_local_near_far}} {
   create_cell -reference DSP48E2 dsp_cell
   set_property -dict {USE_MULT NONE USE_SIMD TWO24 AREG 0 ACASCREG 0 BREG 0 BCASCREG 0 CREG 0 DREG 0 ADREG 0 MREG 0 PREG 0 ALUMODEREG 0 CARRYINREG 0 CARRYINSELREG 0 INMODEREG 0 OPMODEREG 0} [get_cells dsp_cell]
   place_cell [get_cells dsp_cell] DSP48E2_X1Y96
   set_property -dict {DONT_TOUCH true IS_LOC_FIXED true IS_BEL_FIXED true} [get_cells dsp_cell]
   set clkNet [get_nets -of_objects [get_pins src_00/C]]
   set zeroNet [get_nets -of_objects [get_pins src_00/R]]
   set oneNet [get_nets -of_objects [get_pins src_00/CE]]
   foreach p [get_pins -of_objects [get_cells dsp_cell] -filter {DIRECTION == IN}] {
    set tail [file tail $p]
    if {$tail eq {A[6]}} {
     if {$mode eq "lut_dsp_receiver"} {set net [get_nets target]} else {set net [get_nets -of_objects [get_pins src_00/Q]]}
    } elseif {$tail eq "CLK"} {set net $clkNet} elseif {$tail in {{OPMODE[0]} {OPMODE[1]}} || [string match CE* $tail]} {set net $oneNet} else {set net $zeroNet}
    connect_net -net $net -objects $p
   }
   if {$mode eq "dsp_driver_local_near_far"} {
    disconnect_net -net [get_nets target] -objects [get_pins lut_driver/O]
    connect_net -net [get_nets target] -objects [get_pins {dsp_cell/P[24]}]
   } else {
    create_net dsp_observe
    connect_net -net [get_nets dsp_observe] -objects [get_pins {dsp_cell/P[24] dst_30/D}]
   }
   write_checkpoint $dir/unrouted-dsp.dcp
   close_design
   open_checkpoint $dir/unrouted-dsp.dcp
  }
  set driver [expr {$mode eq "ff_lower_local_upper" ? "src_24/Q" : "lut_driver/O"}]
  if {$mode eq "dsp_driver_local_near_far"} {set driver {dsp_cell/P[24]}}
  e2_sll::refresh_access_index
  set roots [get_nodes -of_objects [get_site_pins -of_objects [e2_sll::physical_pins $driver]]]
  save $dir/roots.txt $roots
  puts "PROBE_ROOTS $mode $roots"
  set seg12 [directed_segment LAG_LAG_X78Y530/UBUMP9 SLR1 SLR2]
  set seg23 [directed_segment LAG_LAG_X134Y775/UBUMP9 SLR2 SLR3]
  set tree {}
  proc leaf {i slr} {global mode;set suffix [expr {$mode eq "lut_shared_control_access" ? "CE" : "D"}];return [dict create pin dst_$i/$suffix slr $slr]}
  proc cross {node from to children} {return [dict create resource $node from $from to $to segment [directed_segment $node SLR$from SLR$to] children $children]}
  if {$mode in {ff_lower_local_upper lut_lower_local_upper}} {
   set tree [list [leaf 26 1] [leaf 27 1] [cross LAG_LAG_X52Y290/UBUMP9 1 0 [list [leaf 24 0] [leaf 25 0]]] [cross LAG_LAG_X96Y530/UBUMP9 1 2 [list [leaf 28 2] [leaf 29 2]]]]
  } elseif {$mode eq "lut_two_remote_groups"} {
   set tree [list [leaf 24 1] [leaf 25 1] [cross LAG_LAG_X78Y530/UBUMP9 1 2 [list [leaf 26 2] [leaf 27 2]]] [cross LAG_LAG_X96Y530/UBUMP9 1 2 [list [leaf 28 2] [leaf 29 2]]]]
  } else {
   set far [cross LAG_LAG_X134Y775/UBUMP9 2 3 [list [leaf 28 3] [leaf 29 3]]]
   set remote [list [leaf 26 2] [leaf 27 2] $far]
   if {$mode eq "lut_bram_multi_access"} {lappend remote [dict create pin {bram_receiver/ADDRBWRADDR[6]} slr 2]}
   if {$mode eq "lut_remote_only"} {set remote [concat [list [leaf 24 2] [leaf 25 2]] $remote]} else {set tree [list [leaf 24 1] [leaf 25 1]]}
   if {$mode eq "lut_dsp_receiver"} {lappend tree [dict create pin {dsp_cell/A[6]} slr 1]}
   lappend tree [cross LAG_LAG_X78Y530/UBUMP9 1 2 $remote]
  }
  place_cell [get_cells src_31] SLICE_X5Y270/AFF
  place_cell [get_cells dst_31] SLICE_X5Y692/AFF
  create_net parallel_second
  connect_net -net [get_nets parallel_second] -objects [get_pins {src_31/Q dst_31/D}]
  set secondTree [list [cross LAG_LAG_X78Y530/UBUMP10 1 2 [list [dict create pin dst_31/D slr 2]]]]
  set plan [list [dict create driver $driver source_slr 1 tree $tree] [dict create driver src_31/Q source_slr 1 tree $secondTree]]
  save $dir/plan.tcl [list set e2_sll_plan $plan]
  save $dir/tools.txt [list vivado [version -short] part [get_property PART [current_design]] clock_ns [get_property PERIOD [get_clocks core_clk]]]
  e2_sll::apply_parallel $dir/plan.tcl $dir 2
  if {$mode in {lut_dsp_receiver dsp_driver_local_near_far}} {
   set macroPin [expr {$mode eq "lut_dsp_receiver" ? "dsp_cell/A\[6\]" : "dsp_cell/P\[24\]"}]
   set leaves [get_property NAME [e2_sll::physical_pins $macroPin]]
   if {$leaves eq $macroPin} {error "DSP transformation was not exercised"}
   save $dir/dsp-physical-leaves.txt [list $macroPin $leaves]
  }
  if {$mode in {lut_lower_local_upper lut_two_remote_groups}} {
   set record {};foreach r $e2_sll::records {if {[dict get $r net] eq "target"} {set record $r}}
   if {[dict get $record source_mode] ne "vivado_import_selected_source" || [llength [dict get $record roots]]<2 || [llength [dict get $record import_selected_outputs]]!=1} {error "Native source selection was not exercised"}
  }
  save $dir/normalized.txt [get_property FIXED_ROUTE [get_nets target]]
  route_design
  if {$mode eq "lut_internal_receiver"} {
   set internalNodes [get_nodes -quiet -of_objects [get_site_pins -quiet -of_objects [get_pins dst_24/D]]]
   if {[llength $internalNodes]} {error "Internal receiver was not exercised: $internalNodes"}
   save $dir/internal-receiver.txt [list receiver dst_24/D bels [get_bel_pins -of_objects [get_pins dst_24/D]] net_bels [get_bel_pins -of_objects [get_nets target]] route_status [get_property ROUTE_STATUS [get_nets target]]]
  }
  e2_sll::audit $dir
  save $dir/route.txt [get_property ROUTE [get_nets target]]
  save $dir/route-status.txt [get_property ROUTE_STATUS [get_nets target]]
  set f [open $dir/pips.tsv w]
  foreach p [get_pips -of_objects [get_nets target]] {puts $f "[get_nodes -uphill -of_objects $p]\t[get_nodes -downhill -of_objects $p]"};close $f
  set f [open $dir/sinks.tsv w];foreach i {24 25 26 27 28 29} {set p [dict get [leaf $i 0] pin];puts $f "$p\t[get_nodes -quiet -of_objects [get_site_pins -quiet -of_objects [get_pins $p]]]"};close $f
  save $dir/sll.txt [get_nodes -of_objects [get_nets target] -filter {IS_SLL}]
  write_checkpoint $dir/routed.dcp
  e2_sll::audit_parallel $dir/routed.dcp $dir 2
  set expected [get_property FIXED_ROUTE [get_nets target]]
  close_design
  open_checkpoint $dir/routed.dcp
  if {[get_property FIXED_ROUTE [get_nets target]] ne $expected} {error "Constraint persistence mismatch"}
  file mkdir $dir/readback
  e2_sll::audit $dir/readback
  set savedRecords $e2_sll::records
  set first {};foreach r $e2_sll::records {if {[dict get $r net] eq "target"} {set first $r}}
  dict set first loads [dict get [leaf 24 0] pin] {intentionally_wrong_sll}
  set e2_sll::records [list $first]
  file mkdir $dir/negative
  set rejected [catch {e2_sll::audit $dir/negative} why]
  set e2_sll::records $savedRecords
  if {!$rejected || ![string match {*SLL path mismatch*} $why]} {error "Negative receiver assignment was not rejected: $why"}
  save $dir/negative/result.txt $why
 } msg opts]
 save $dir/status.txt [list code $rc message $msg options $opts]
 puts "PROBE_DONE $mode $rc $msg"
 if {$rc} {incr failures}
}
exit [expr {$failures!=0}]
