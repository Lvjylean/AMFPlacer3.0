# Two existing placed DCPs are routed as-is, once each. No placement or incremental import.
lassign $argv out input mode helper plan
file mkdir $out
set_param general.maxThreads 4
set stages [open $out/stages.tsv w];puts $stages "stage\tseconds\treturn_code"
set obs [open $out/observations.tsv w];puts $obs "key\tvalue"
proc note {key value} {global obs;puts $obs "$key\t[string map [list \n { | } \t { }] $value]";flush $obs}
proc stage {name body} {
 global stages out
 puts "ROUTE_PAIR_STAGE_START $name";flush stdout
 set start [clock milliseconds]
 set rc [catch {uplevel 1 $body} value options]
 set elapsed [expr {([clock milliseconds]-$start)/1000.0}]
 puts $stages "$name\t$elapsed\t$rc";flush $stages
 puts "ROUTE_PAIR_STAGE_FINISH $name $elapsed $rc";flush stdout
 if {$rc} {set f [open $out/error-$name.txt w];puts $f $value;puts $f [dict get $options -errorinfo];close $f}
 return [list $rc $value]
}
proc placement_snapshot {label} {
 global beforePlacement out
 set objects [get_cells -hierarchical -filter {IS_PRIMITIVE}]
 set names [get_property NAME $objects];set locs [get_property LOC $objects];set bels [get_property BEL $objects]
 set locFixed 0;foreach v [get_property IS_LOC_FIXED $objects] {if {$v in {1 true}} {incr locFixed}}
 set belFixed 0;foreach v [get_property IS_BEL_FIXED $objects] {if {$v in {1 true}} {incr belFixed}}
 set lutPins 0;set luts [get_cells -hierarchical -filter {REF_NAME =~ LUT*}]
 foreach v [get_property LOCK_PINS $luts] {if {[string trim $v "{} \t\n"] ne ""} {incr lutPins}}
 note $label.primitive_cells [llength $objects];note $label.loc_fixed $locFixed;note $label.bel_fixed $belFixed;note $label.luts_with_lock_pins $lutPins
 if {$label eq "before"} {
  set beforePlacement {}
  foreach n $names l $locs b $bels {dict set beforePlacement $n [list $l $b]}
 } else {
  set changed 0;set added 0;set missing 0;set seen {}
  set f [open $out/placement-changes.tsv w];puts $f "cell\tbefore\tafter"
  foreach n $names l $locs b $bels {
   dict set seen $n 1
   if {![dict exists $beforePlacement $n]} {incr added;puts $f "$n\tMISSING\t$l $b"} elseif {[dict get $beforePlacement $n] ne [list $l $b]} {incr changed;puts $f "$n\t[dict get $beforePlacement $n]\t$l $b"}
  }
  dict for {n value} $beforePlacement {if {![dict exists $seen $n]} {incr missing;puts $f "$n\t$value\tMISSING"}}
  close $f;note placement_changed $changed;note cells_added $added;note cells_missing $missing
 }
}
proc sll_inventory {} {
 global out
 set tiles [get_tiles -of_objects [get_slrs] LAG_LAG*]
 set nodes [get_nodes -of_objects $tiles LAG* -filter {IS_CROSSING_SLRS}]
 set nets [get_nets -quiet -of_objects $nodes]
 set f [open $out/sll-used.tsv w];puts $f "net\tnet_type\troute_status\tnode\tphysical_column\tcrossing_slrs"
 set used {};set perColumn {};set netCount 0;set owners {}
 foreach net $nets {
  set nn [get_property NAME $net];set nt [get_property TYPE $net];set rs [get_property ROUTE_STATUS $net]
  set actual [get_nodes -quiet -of_objects $net -filter {IS_CROSSING_SLRS && IS_SLL}]
  if {![llength $actual]} {continue};incr netCount
  foreach node $actual {
   set name [get_property NAME $node]
   set cols [lsort -unique [get_property COLUMN [get_tiles -of_objects $node]]]
   if {[llength $cols]!=1} {error "SLL does not map to one physical COLUMN: $name $cols"}
   set col [lindex $cols 0];set seam [join [get_property CROSSING_SLRS $node] ,]
   puts $f "$nn\t$nt\t$rs\t$name\t$col\t$seam"
   if {![dict exists $used $name]} {dict set used $name 1;dict incr perColumn [list $seam $col]}
   dict lappend owners $name $nn
  }
 }
 close $f
 set shared 0;dict for {n ns} $owners {if {[llength [lsort -unique $ns]]>1} {incr shared}}
 set f [open $out/sll-columns.tsv w];puts $f "crossing_slrs\tphysical_column\tused_sll_nodes"
 foreach key [lsort -dictionary [dict keys $perColumn]] {lassign $key seam col;puts $f "$seam\t$col\t[dict get $perColumn $key]"};close $f
 note sll_unique_nodes [dict size $used];note sll_net_objects $netCount;note sll_nodes_with_multiple_net_objects $shared
 note sll_inventory_scope {Current route graph; partial data only when route_design failed or routing is incomplete.}
}
proc planned_records {items prefix loadsVar segmentsVar} {
 upvar 1 $loadsVar loads $segmentsVar segments
 foreach c $items {
  if {[dict exists $c pin]} {dict set loads [dict get $c pin] $prefix} else {
   set resource [dict get $c resource];dict set segments $resource [dict get $c segment]
   planned_records [dict get $c children] [concat $prefix [list $resource]] loads segments
  }
 }
}
proc grouped_audit {} {
 global helper plan out
 source $helper
 e2_sll::refresh_access_index
 source $plan
 set e2_sll::records {}
 foreach record $e2_sll_plan {
  set loads {};set segments {};planned_records [dict get $record tree] {} loads segments
  set driver [dict get $record driver]
  lappend e2_sll::records [dict create net [get_property NAME [e2_sll::driver_net $driver]] driver $driver source_slr [dict get $record source_slr] loads $loads segments $segments]
 }
 # Only audit the existing routes; do not call e2_sll::apply or mutate constraints.
 e2_sll::audit $out
}
lassign [stage open {open_checkpoint $input}] rc message
if {$rc} {note state open_failed;close $obs;close $stages;exit 1}
source [file join [file dirname [info script]] sll_routes.tcl]
source [file join [file dirname [info script]] sll_parallel.tcl]
source [file join [file dirname [info script]] sll_usage.tcl]
source [file join [file dirname [info script]] native_roots.tcl]
note input_dcp $input;note source_choice {Vivado native routing evidence, no AMF-selected arbitrary source}
note vivado_version [version -short];note part [get_property PART [current_design]]
set clocks [get_clocks]
if {[llength $clocks]!=1 || abs([get_property PERIOD $clocks]-10.0)>1e-6} {error "Expected unchanged 10 ns clock"}
note clock_period_ns [get_property PERIOD $clocks]
report_clocks -file $out/clocks.rpt
lassign [stage initial_placement {placement_snapshot before}] rc message
if {$rc} {exit 2}
lassign [stage source_root_repair {
 set objects {}
 dict for {name root} $nativeRoots {set net [get_nets $name];reset_property FIXED_ROUTE $net;lappend objects $net}
 route_design -unroute -nets $objects
 source [file join [file dirname [info script]] native_source_repair.xdc]
 dict for {name root} $nativeRoots {
  set net [get_nets $name];set selected [e2_sll::source_accesses [get_property FIXED_ROUTE $net] [list $root]]
  if {$selected ne $root} {error "Native root repair was not retained on $name"}
 }
 note repaired_source_roots [dict size $nativeRoots]
}] rc message
if {$rc} {exit 2}
lassign [stage placed_checkpoint {write_checkpoint $out/getrf_placed.dcp}] rc message
if {$rc} {exit 2}
lassign [stage route {route_design}] routeRC routeMessage
note route_return_code $routeRC;note route_message $routeMessage
note route_design_whole_design_invocations 1
foreach {name body} {
 route_audit {placement_snapshot after}
 reports {
  report_route_status -file $out/route_status.rpt
  report_timing_summary -delay_type min_max -report_unconstrained -file $out/timing_summary.rpt
  report_drc -no_waivers -file $out/drc.rpt
  report_utilization -file $out/utilization.rpt
  set f [open $out/drc_counts.json w]
  puts $f [format {{"errors":%d,"critical_warnings":%d}} [llength [get_drc_violations -quiet -filter {SEVERITY == Error}]] [llength [get_drc_violations -quiet -filter {SEVERITY == "Critical Warning"}]]];close $f
 }
 checkpoint {write_checkpoint $out/getrf_routed.dcp}
 sll_inventory {e2_sll::usage $out}
} {lassign [stage $name $body] rc message;note $name.return_code $rc}
set e2_sll::records {}
foreach shard [lsort [glob -directory $out/sll-parallel shard-*]] {
 source $shard/apply/records.tcl
 set e2_sll::records [concat $e2_sll::records $e2_sll_worker_records]
}
note audited_net_records [llength $e2_sll::records]
lassign [stage sll_path_audit {e2_sll::audit_parallel $out/getrf_routed.dcp $out 4}] auditRC auditMessage
note sll_path_audit.return_code $auditRC;note sll_path_audit.message $auditMessage
if {!$routeRC && !$auditRC} {
 source [file join [file dirname [info script]] export_boundary_timing_samples.tcl]
 lassign [stage boundary_samples {export_boundary_timing_samples $out/physical}] boundaryRC boundaryMessage
 note boundary_samples.return_code $boundaryRC
}
note state [expr {$routeRC || $auditRC ? "failed" : "completed"}]
close $obs;close $stages
exit [expr {$routeRC || $auditRC}]
