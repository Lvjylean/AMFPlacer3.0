lassign $argv mode checkpoint shard helper
set out [file join $shard $mode]
set_param general.maxThreads 1
if {[catch {
 open_checkpoint $checkpoint
 source $helper
 if {[info exists ::env(AMF_SLL_DFX_ROUTING)] && $::env(AMF_SLL_DFX_ROUTING) eq "1"} {set ::e2_sll::allowDfxRouting 1}
 if {$mode eq "apply"} {
  e2_sll::apply [file join $shard plan.tcl] $out
  set f [open [file join $out records.tcl] w]
  puts $f [list set ::e2_sll_worker_records $e2_sll::records];close $f
  set f [open [file join $out locks.tcl] w]
  puts $f [list set ::e2_sll_worker_locks $e2_sll::exportLocks];close $f
 } elseif {$mode eq "audit"} {
  source [file join $shard apply records.tcl]
  set e2_sll::records $::e2_sll_worker_records
  e2_sll::refresh_access_index
  e2_sll::audit $out
 } else {error "Unknown SLL worker mode"}
} failure options]} {
 puts stderr "E2_SLL_WORKER_FAILED $failure"
 puts stderr [dict get $options -errorinfo]
 exit 1
}
puts "E2_SLL_WORKER_COMPLETED $mode"
exit 0
