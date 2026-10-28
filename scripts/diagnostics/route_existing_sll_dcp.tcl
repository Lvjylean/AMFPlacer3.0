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
note mode $mode;note input_dcp $input;note vivado_version [version -short];note part [get_property PART [current_design]];note max_threads [get_param general.maxThreads]
set clocks [get_clocks]
if {[llength $clocks]!=1 || abs([get_property PERIOD $clocks]-10.0)>1e-6} {error "Expected unchanged single 10 ns clock"}
note clock_period_ns [get_property PERIOD $clocks]
report_clocks -file $out/clocks.rpt
lassign [stage initial_placement {placement_snapshot before}] rc message
if {$rc} {note state initial_audit_failed;close $obs;close $stages;exit 1}
# One default whole-design call, no directive override, no incremental options.
lassign [stage route {route_design}] routeRC routeMessage
note route_return_code $routeRC;note route_message $routeMessage
note routing_executed 1;note route_design_invocations 1
foreach {name body} {
 route_status {report_route_status -file $out/route-status.rpt}
 timing {report_timing_summary -delay_type min_max -report_unconstrained -file $out/timing-summary.rpt}
 drc {report_drc -no_waivers -file $out/drc.rpt}
 placement_after {placement_snapshot after}
 checkpoint {write_checkpoint $out/route_attempt.dcp}
 sll_usage {sll_inventory}
} {lassign [stage $name $body] rc message;note $name.return_code $rc}
if {$mode eq "sll-fixed" && !$routeRC} {lassign [stage receiver_group_audit {grouped_audit}] rc message;note receiver_group_audit.return_code $rc;note receiver_group_audit.message $message} else {note receiver_group_audit.skipped {No planned-path requirement for free routing; skip proof on failed fixed routing.}}
note state [expr {$routeRC ? "route_failed" : "route_command_completed"}]
close $obs;close $stages
exit $routeRC
