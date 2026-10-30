namespace eval e2_sll {variable auditScriptDir [file dirname [file normalize [info script]]]}
proc e2_sll::audit_worker_budget {requested} {
 if {![string is integer -strict $requested] || $requested<1 || $requested>8} {error "SLL audit workers must be 1 through 8"}
 if {![file exists /proc/meminfo]} {return 1}
 set f [open /proc/meminfo r];set memory [read $f];close $f
 if {![regexp {MemAvailable:\s+(\d+)} $memory -> kib]} {return 1}
 # Keep 16 GiB headroom and budget 20 GiB per checkpoint reader. If the
 # budget permits only one worker, stay in-session and avoid reopening.
 set allowed [expr {max(1,int(($kib*1024.0-16*pow(1024,3))/(20*pow(1024,3))))}]
 return [expr {min($requested,$allowed)}]
}
# A supplied checkpoint must be the current routed design just saved by the
# caller. Full flow passes base_routed.dcp; incremental flow leaves this empty
# so the audit snapshots its current joint design before starting any worker.
proc e2_sll::audit_with_workers {out requested {checkpoint {}}} {
 variable records;variable auditScriptDir
 set started [clock microseconds];set work 0
 foreach record $records {incr work [dict size [dict get $record loads]]}
 set workers [audit_worker_budget $requested]
 if {[llength $records]<=64 || $work<=4096 || $workers<=1} {
  audit $out
  return
 }
 set parallel [file join $out audit-parallel]
 if {[file exists $parallel]} {error "Refusing to overwrite completed or failed SLL audit shards"}
 file mkdir $parallel
 if {$checkpoint eq ""} {
  set checkpoint [file join $parallel audit-input.dcp]
  write_checkpoint $checkpoint
 } elseif {![file isfile $checkpoint]} {error "Missing routed audit checkpoint"}
 set workers [expr {min($workers,[llength $records])}]
 set weights [lrepeat $workers 0];set ranked {}
 for {set i 0} {$i<$workers} {incr i} {set buckets($i) {}}
 foreach record $records {lappend ranked [list [expr {128+[dict size [dict get $record loads]]}] $record]}
 set ef [open [file join $parallel expected.tsv] w];set seen {}
 set rf [open [file join $parallel expected-receivers.tsv] w]
 foreach pair [lsort -integer -decreasing -index 0 $ranked] {
  lassign $pair weight record
  set name [dict get $record net]
  if {[dict exists $seen $name]} {close $ef;close $rf;error "Repeated audit net $name"}
  dict set seen $name 1
  puts $ef [join [list $name [dict size [dict get $record loads]]] "\t"]
  dict for {pin path} [dict get $record loads] {puts $rf [join [list $name $pin [join $path ,]] "\t"]}
  set best 0
  for {set i 1} {$i<$workers} {incr i} {if {[lindex $weights $i]<[lindex $weights $best]} {set best $i}}
  lappend buckets($best) $record
  lset weights $best [expr {[lindex $weights $best]+$weight}]
 }
 close $ef;close $rf
 for {set i 0} {$i<$workers} {incr i} {
  set dir [file join $parallel [format shard-%02d $i] apply];file mkdir $dir
  set f [open [file join $dir records.tcl] w]
  puts $f [list set ::e2_sll_worker_records $buckets($i)];close $f
 }
 puts "E2_SLL_PARALLEL_AUDIT_START workers=$workers receivers=$work weights=$weights";flush stdout
 exec python3 [file join $auditScriptDir sll_parallel.py] audit $checkpoint $parallel $workers >[file join $parallel controller.log] 2>@1
 exec python3 [file join $auditScriptDir sll_audit_collect.py] $parallel $out >[file join $parallel collect.log] 2>@1
 set f [open [file join $out sll-audit-parallel-timings.tsv] w]
 puts $f "phase\tseconds";puts $f "total\t[expr {([clock microseconds]-$started)/1e6}]";close $f
 puts "E2_SLL_PARALLEL_AUDIT_COMPLETED nets=[llength $records] receivers=$work workers=$workers"
}
