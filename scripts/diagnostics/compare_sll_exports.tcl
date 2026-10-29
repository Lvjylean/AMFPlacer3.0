lassign $argv reference candidate
proc read_export {dir} {
 source $dir/records.tcl
 source $dir/locks.tcl
 source $dir/normalized.tcl
 set records {}
 foreach record $benchmark_records {dict set records [dict get $record net] $record}
 return [list $records $benchmark_locks $benchmark_routes]
}
lassign [read_export $reference] oldRecords oldLocks oldRoutes
lassign [read_export $candidate] newRecords newLocks newRoutes
foreach old [list $oldRecords $oldLocks $oldRoutes] new [list $newRecords $newLocks $newRoutes] label {records locks normalized_routes} {
 if {[lsort [dict keys $old]] ne [lsort [dict keys $new]]} {error "$label coverage mismatch"}
 dict for {name value} $old {if {$value ne [dict get $new $name]} {error "$label mismatch: $name"}}
 puts "PASS $label [dict size $old]"
}
