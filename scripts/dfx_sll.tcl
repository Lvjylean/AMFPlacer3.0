# Apply worker trees only after all modules are assembled into the locked shell.
source [file join [file dirname [info script]] sll_routes.tcl]
source [file join [file dirname [info script]] sll_parallel.tcl]
namespace eval dfx_sll {variable active 0}
proc dfx_sll::validate_tree {tree prefix footprint usedVar} {
 upvar 1 $usedVar used
 foreach item $tree {
  if {[dict exists $item pin]} {
   if {[string first $prefix [dict get $item pin]]!=0} {error "SLL receiver leaves active RP"}
  } else {
   foreach node [dict get $item segment] {
    if {![dict exists $footprint [lindex [split $node /] 0]]} {error "SLL segment outside DFX route footprint: $node"}
    if {[dict exists $used $node]} {error "SLL plans collide at $node"}
    dict set used $node 1
    if {[llength [get_nets -quiet -of_objects [get_nodes $node]]]} {error "SLL node already used by frozen routing: $node"}
   }
   validate_tree [dict get $item children] $prefix $footprint used
  }
 }
}
proc dfx_sll::apply {} {
 global dfx
 variable active
 set combined {};set used {}
 foreach row $dfx(rps) {
  lassign $row id rp pb
  set file [file join $dfx(worker_$id) sll-plan.tcl]
  if {![file exists $file]} {
   if {[info exists dfx(e2_sll_enabled)] && $dfx(e2_sll_enabled)} {error "Missing worker SLL plan $id"}
   continue
  }
  source $file
  set footprint {}
  foreach tile [get_property NAME [get_dfx_footprint -route -of_objects [get_cells $rp]]] {dict set footprint $tile 1}
  foreach record $e2_sll_plan {
   if {[string first "$rp/" [dict get $record driver]]!=0} {error "SLL driver leaves active RP"}
   validate_tree [dict get $record tree] "$rp/" $footprint used
   lappend combined $record
  }
 }
 if {![llength $combined]} {return}
 set active 1
 set dir [file join $dfx(output) sll];file mkdir $dir
 set file [file join $dir combined-plan.tcl]
 set f [open $file w];puts $f [list set e2_sll_plan $combined];close $f
 # Ordinary routing remains free inside the pre-existing DFX footprint.
 set ::e2_sll::allowDfxRouting 1
 set ::env(AMF_SLL_DFX_ROUTING) 1
 if {[info exists dfx(vivado)]} {set ::env(AMF_SLL_VIVADO) $dfx(vivado)}
 set workers 4
 if {[info exists dfx(e2_sll_workers)]} {set workers $dfx(e2_sll_workers)}
 e2_sll::apply_parallel $file $dir $workers
 set f [open [file join $dir records.tcl] w];puts $f [list set ::e2_sll::records $::e2_sll::records];close $f
 puts "E2_INCREMENTAL_SLL_APPLIED nets=[llength $combined]"
}
proc dfx_sll::audit {} {
 global dfx
 variable active
 if {$active} {e2_sll::audit [file join $dfx(output) sll]}
}
