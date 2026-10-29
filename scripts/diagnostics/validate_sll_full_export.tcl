lassign $argv checkpoint plan oldParallel out
set scriptDir [file dirname [file dirname [file normalize [info script]]]]
if {[file exists $out]} {error "Refusing to overwrite $out"}
file mkdir $out
proc save {path value} {set f [open $path w];puts $f $value;close $f}
proc read_xdc_routes {path} {
 set child [interp create]
 try {
  interp eval $child {
   set routes {}
   proc get_nets {name} {return $name}
   proc set_property {property route net} {
    if {$property ne "FIXED_ROUTE"} {error "unexpected exported property"}
    if {[dict exists $::routes $net]} {error "duplicate XDC net"}
    dict set ::routes $net $route
   }
  }
  interp eval $child [list source $path]
  return [interp eval $child {set routes}]
 } finally {interp delete $child}
}
set_param general.maxThreads 1
if {[catch {
 open_checkpoint $checkpoint
 source $scriptDir/dfx_sll.tcl
 set ::e2_sll::allowDfxRouting 1
 set ::env(AMF_SLL_DFX_ROUTING) 1
 set ::env(AMF_SLL_VIVADO) /Projects/Xilinx26/2026.1/Vivado/bin/vivado
 set clocks [get_property PERIOD [get_clocks]]
 set started [clock microseconds]
 set filtered [dfx_sll::prepare_full_plan $plan $out]
 set filterSeconds [expr {([clock microseconds]-$started)/1e6}]
 set started [clock microseconds]
 e2_sll::apply_parallel $filtered $out 4
 set exportSeconds [expr {([clock microseconds]-$started)/1e6}]
 set oldRecords {}
 foreach path [lsort [glob -directory $oldParallel shard-*/apply/records.tcl]] {
  source -notrace $path
  foreach r $::e2_sll_worker_records {dict set oldRecords [dict get $r net] $r}
 }
 set mismatches {};set receiverCount 0;set sllCount 0
 foreach record $::e2_sll::records {
  set name [dict get $record net]
  if {![dict exists $oldRecords $name]} {error "unexpected net $name"}
  set original [dict get $oldRecords $name]
  foreach key {driver source_slr roots source_mode loads segments import_selected_outputs} {
   if {[dict get $record $key] ne [dict get $original $key]} {lappend mismatches [list $name $key]}
  }
  incr receiverCount [dict size [dict get $record loads]]
  incr sllCount [dict size [dict get $record segments]]
  if {![dfx_sll::route_has_content [get_property FIXED_ROUTE [get_nets [list $name]]]]} {error "missing imported constraint $name"}
 }
 if {[llength $mismatches]} {save $out/mismatches.tcl $mismatches;error "export record mismatch count=[llength $mismatches]"}
 set oldRoutes [read_xdc_routes [file join [file dirname $oldParallel] sll-fixed-route.xdc]]
 set newRoutes [read_xdc_routes $out/sll-fixed-route.xdc]
 if {[dict size $newRoutes]!=[llength $::e2_sll::records]} {error "XDC coverage mismatch"}
 dict for {name route} $newRoutes {
  if {![dict exists $oldRoutes $name] || [dict get $oldRoutes $name] ne $route} {error "XDC route tree changed: $name"}
 }
 if {[get_property PERIOD [get_clocks]] ne $clocks} {error "clocks changed"}
 # Verify all endpoint LOC/BEL against the index made after checkpoint load.
 set checked {}
 foreach path [glob -directory $out/sll-parallel shard-*/apply/locks.tcl] {
  source -notrace $path
  dict for {name value} $::e2_sll_worker_locks {
   if {[dict exists $checked $name]} {continue}
   dict set checked $name 1
   lassign $value loc bel mapping
   set c [dict get $::e2_sll::cellsByName $name]
   if {[get_property LOC $c] ne $loc || [get_property BEL $c] ne $bel} {error "placement changed $name"}
   if {$mapping ne {} && [get_property LOCK_PINS $c] ne $mapping} {error "pin lock changed $name"}
  }
 }
 save $out/summary.tsv "nets\t[llength $::e2_sll::records]\nreceivers\t$receiverCount\nsll_crossings\t$sllCount\nendpoint_cells\t[dict size $checked]\nnative_interfaces\t[llength $::dfx_sll::nativeInterfaces]\nclassification_seconds\t$filterSeconds\nexport_seconds\t$exportSeconds\nclocks\t$clocks\nrecord_mismatches\t0\nxdc_tree_mismatches\t0"
 save $out/status.txt completed
} failure options]} {
 save $out/status.txt [list failed $failure $options]
 puts stderr [dict get $options -errorinfo];exit 1
}
puts "SLL_FULL_EXPORT_VALIDATED seconds=$exportSeconds"
exit 0
