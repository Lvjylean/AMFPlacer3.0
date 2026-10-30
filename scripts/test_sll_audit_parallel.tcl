source [file join [file dirname [info script]] sll_audit_parallel.tcl]
set requestedCalls {}
rename e2_sll::audit_worker_budget e2_sll::audit_worker_budget_native
proc e2_sll::audit_worker_budget {n} {return $n}
proc e2_sll::audit {out} {incr ::inSession}
proc exec {args} {lappend ::requestedCalls $args;return {}}
set writes 0
proc write_checkpoint {path} {incr ::writes;set f [open $path w];puts $f snapshot;close $f}
set inSession 0
set temp [file join [file dirname [info script]] audit-unit-[pid]]
file mkdir $temp
set rc [catch {
 set e2_sll::records {}
 for {set i 0} {$i<65} {incr i} {
  set loads {}
  for {set j 0} {$j<70} {incr j} {dict set loads pin$j {sll}}
  lappend e2_sll::records [dict create net net$i loads $loads]
 }
 set checkpoint [file join $temp routed.dcp]
 set f [open $checkpoint w];puts $f unit;close $f
 e2_sll::audit_with_workers $temp 3 $checkpoint
 if {[llength $requestedCalls]!=2} {error "Expected one worker dispatcher and one strict collector"}
 set seen {};set count 0
 foreach path [glob -directory [file join $temp audit-parallel] shard-*/apply/records.tcl] {
  source $path
  foreach record $::e2_sll_worker_records {
   set name [dict get $record net]
   if {[dict exists $seen $name]} {error "Repeated partitioned net"}
   dict set seen $name 1;incr count
  }
 }
 if {$count!=65 || [dict size $seen]!=65} {error "Partition omitted a net"}
 if {![catch {e2_sll::audit_with_workers $temp 3 $checkpoint}]} {error "Overwrote existing audit"}
 set second [file join $temp new-snapshot];file mkdir $second
 e2_sll::audit_with_workers $second 3
 if {$writes!=1 || ![file isfile [file join $second audit-parallel audit-input.dcp]]} {error "Did not save fresh audit snapshot"}
 if {![catch {e2_sll::audit_worker_budget_native 0}]} {error "Invalid worker count accepted"}
 set e2_sll::records [lrange $e2_sll::records 0 1]
 e2_sll::audit_with_workers $temp 3 $checkpoint
 if {$inSession!=1} {error "Small audit did not stay in session"}
} message options]
file delete -force $temp
if {$rc} {return -options $options $message}
puts SLL_AUDIT_PARALLEL_TESTS_PASSED
