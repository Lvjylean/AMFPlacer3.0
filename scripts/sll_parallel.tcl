namespace eval e2_sll {variable parallelScriptDir [file dirname [file normalize [info script]]]}
proc e2_sll::tree_weight {items} {
 set total 0
 foreach child $items {
  if {[dict exists $child pin]} {incr total} else {incr total [expr {8+[tree_weight [dict get $child children]]}]}
 }
 return $total
}
proc e2_sll::partition_plan {e2_sll_plan workers} {
 set weights [lrepeat $workers 0]
 for {set i 0} {$i<$workers} {incr i} {set buckets($i) {}}
 set ranked {}
 foreach record $e2_sll_plan {lappend ranked [list [tree_weight [dict get $record tree]] $record]}
 foreach pair [lsort -integer -decreasing -index 0 $ranked] {
  lassign $pair weight record;set best 0
  for {set i 1} {$i<$workers} {incr i} {if {[lindex $weights $i]<[lindex $weights $best]} {set best $i}}
  lappend buckets($best) $record
  lset weights $best [expr {[lindex $weights $best]+$weight}]
 }
 set partitions {}
 for {set i 0} {$i<$workers} {incr i} {lappend partitions $buckets($i)}
 return [list $partitions $weights]
}
proc e2_sll::apply_parallel {planFile out workers} {
 variable parallelScriptDir;variable records;variable cellsByName;variable cellLocations;variable cellBels
 if {![string is integer -strict $workers]||$workers<1||$workers>8} {error "SLL workers must be 1 through 8"}
 set parallel [file join $out sll-parallel]
 if {[file exists $parallel]} {error "Refusing to overwrite SLL parallel output"}
 file mkdir $parallel
 source -notrace $planFile
 if {![llength $e2_sll_plan]} {set records {};return}
 set workers [expr {min($workers,[llength $e2_sll_plan])}]
 lassign [partition_plan $e2_sll_plan $workers] partitions weights
 set checkpoint [file join $out getrf_before_sll.dcp]
 write_checkpoint $checkpoint
 for {set i 0} {$i<$workers} {incr i} {
  set dir [file join $parallel [format shard-%02d $i]];file mkdir $dir
  set f [open [file join $dir plan.tcl] w]
  puts $f [list set e2_sll_plan [lindex $partitions $i]];close $f
 }
 puts "E2_SLL_PARALLEL_START workers=$workers weights=$weights";flush stdout
 exec python3 [file join $parallelScriptDir sll_parallel.py] apply $checkpoint $parallel $workers >[file join $parallel apply-controller.log] 2>@1
 refresh_access_index
 set locks {};set records {};set nets {};set nodes {}
 foreach shard [lsort [glob -directory $parallel shard-*]] {
  source -notrace [file join $shard apply records.tcl]
  source -notrace [file join $shard apply locks.tcl]
  foreach record $::e2_sll_worker_records {
   set name [dict get $record net]
   if {[dict exists $nets $name]} {error "Repeated parallel net $name"};dict set nets $name 1
   dict for {resource segment} [dict get $record segments] {
    foreach node $segment {
     if {[dict exists $nodes $node]} {error "Parallel SLL resource collision $node"};dict set nodes $node $name
    }
   }
   lappend records $record
  }
  dict for {name value} $::e2_sll_worker_locks {
   if {[dict exists $locks $name]&&[dict get $locks $name] ne $value} {error "Parallel endpoint mapping disagreement $name"}
   dict set locks $name $value
  }
 }
 if {[llength $records]!=[llength $e2_sll_plan]} {error "Parallel SLL coverage mismatch"}
 set objects {}
 dict for {name value} $locks {
  lassign $value loc bel mapping
  if {![dict exists $cellsByName $name]||[dict get $cellLocations $name] ne $loc||[dict get $cellBels $name] ne $bel} {error "Stale parallel endpoint mapping $name"}
  set object [dict get $cellsByName $name];lappend objects $object
  if {$mapping ne ""} {set_property LOCK_PINS $mapping $object}
 }
 if {[llength $objects]} {set_property IS_LOC_FIXED true $objects;set_property IS_BEL_FIXED true $objects}
 set combined [open [file join $out sll-fixed-route.xdc] w]
 foreach shard [lsort [glob -directory $parallel shard-*]] {
  set file [file join $shard apply sll-fixed-route.xdc]
  set f [open $file r];puts $combined [read $f];close $f
  source -notrace $file
 }
 close $combined
 puts "E2_SLL_PARALLEL_APPLIED [llength $records]";flush stdout
}
proc e2_sll::audit_parallel {checkpoint out workers} {
 variable parallelScriptDir;variable records
 if {![llength $records]} {return}
 if {![string is integer -strict $workers]||$workers<1||$workers>8} {error "SLL workers must be 1 through 8"}
 set parallel [file join $out sll-parallel]
 exec python3 [file join $parallelScriptDir sll_parallel.py] audit $checkpoint $parallel $workers >[file join $parallel audit-controller.log] 2>@1
 puts "E2_SLL_PARALLEL_AUDIT_COMPLETED";flush stdout
}
