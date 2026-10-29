# Short-term DFX compatibility: native routing owns RP interfaces. Ordinary
# static/internal nets keep their exact E2 constraints and original path audit.
namespace eval dfx_sll {variable nativeInterfaces {};variable fullScopeMode legacy-exact;variable fullNativeCount 0}
proc dfx_sll::route_has_content {route} {
 # Vivado may represent an empty DRS as the literal string "{}".
 return [expr {[string trim $route "{} \t\r\n"] ne {}}]
}
proc dfx_sll::rp_owner {pin rps} {
 set owner {}
 foreach rp $rps {
  if {[string first "$rp/" $pin]==0 && [string length $rp]>[string length $owner]} {set owner $rp}
 }
 return $owner
}
proc dfx_sll::tree_pins {tree} {
 set pins {}
 foreach item $tree {
  if {[dict exists $item pin]} {lappend pins [dict get $item pin]} else {lappend pins {*}[tree_pins [dict get $item children]]}
 }
 return $pins
}
proc dfx_sll::tree_resources {tree} {
 set resources {}
 foreach item $tree {
  if {[dict exists $item resource]} {
   lappend resources [dict get $item resource] {*}[tree_resources [dict get $item children]]
  }
 }
 return [lsort -unique $resources]
}
proc dfx_sll::interface_index {} {
 # Index actual RP boundary connectivity, including hierarchical macro pins;
 # classification must not depend only on the reduced AMF receiver tree.
 set objects [get_cells -quiet -hierarchical -filter {HD.RECONFIGURABLE == 1}]
 set rps [get_property NAME $objects];set drivers {}
 if {[llength $objects]} {
  set pins [get_pins -quiet -of_objects $objects]
  if {[llength $pins]} {
   set segments [get_nets -quiet -segments -of_objects $pins]
   if {[llength $segments]} {
    foreach driver [get_property NAME [get_pins -quiet -of_objects $segments -filter {DIRECTION == OUT}]] {dict set drivers $driver 1}
   }
  }
 }
 return [list $rps $drivers]
}
proc dfx_sll::partition_full_plan {plans rps boundaryDrivers} {
 set exact {};set native {}
 foreach plan $plans {
  set driver [dict get $plan driver]
  set boundary [dict exists $boundaryDrivers $driver]
  set owner [rp_owner $driver $rps]
  foreach pin [tree_pins [dict get $plan tree]] {
   if {[rp_owner $pin $rps] ne $owner} {set boundary 1;break}
  }
  if {$boundary} {lappend native $plan} else {lappend exact $plan}
 }
 return [list $exact $native]
}
proc dfx_sll::prepare_full_plan {planFile out} {
 variable nativeInterfaces;variable fullScopeMode;variable fullNativeCount
 set nativeInterfaces {};set fullScopeMode native-interfaces-v1
 source -notrace $planFile
 lassign [interface_index] rps drivers
 lassign [partition_full_plan $e2_sll_plan $rps $drivers] exact native
 if {[llength $native]} {e2_sll::refresh_access_index}
 set seen {};set reroute {}
 foreach plan $native {
  set driver [dict get $plan driver];set net [e2_sll::driver_net $driver]
  set name [get_property NAME $net]
  if {[dict exists $seen $name]} {error "Repeated DFX interface plan: $name"};dict set seen $name 1
  # This procedure runs only before FULL routing, never on a frozen shell.
  # Remove old trial constraints as well when resuming a placed checkpoint.
  set hadConstraint [expr {[route_has_content [get_property FIXED_ROUTE $net]] || [get_property IS_ROUTE_FIXED $net]}]
  set hasStub [regexp {\mSTUB\M} [get_property ROUTE $net]]
  if {$hadConstraint || $hasStub} {
   set_property IS_ROUTE_FIXED false $net
   reset_property FIXED_ROUTE $net
   lappend reroute $net
  }
  lappend nativeInterfaces [dict create driver $driver net $name planned_resources [tree_resources [dict get $plan tree]] old_constraint_cleared $hadConstraint]
 }
 # One batch, not one router invocation per interface.
 if {[llength $reroute]} {route_design -unroute -nets $reroute}
 file mkdir $out
 set filtered [file join $out full-internal-plan.tcl]
 set f [open $filtered w];puts $f [list set e2_sll_plan $exact];close $f
 set f [open [file join $out native-interfaces.tcl] w];puts $f [list set ::dfx_sll::nativeInterfaces $nativeInterfaces];close $f
 set fullNativeCount [llength $nativeInterfaces]
 set f [open [file join $out full-constraint-scope.tsv] w]
 puts $f "mode\tnative-interface-fallback"
 puts $f "original_planned_nets\t[llength $e2_sll_plan]"
 puts $f "exact_constrained_nets\t[llength $exact]"
 puts $f "native_interface_nets\t$fullNativeCount"
 puts $f "all_planned_positions_enforced\t[expr {!$fullNativeCount}]"
 close $f
 puts "E2_DFX_NATIVE_INTERFACES count=$fullNativeCount exact_internal_nets=[llength $exact]"
 return $filtered
}
proc dfx_sll::audit_native_interfaces {out} {
 variable nativeInterfaces
 set f [open [file join $out native-interface-route-audit.tsv] w]
 puts $f "net\tmode\troute_status\thas_stub\tplanned_sll\tactual_sll\tpositions_match\troute_ok"
 set failures {}
 try {
  foreach record $nativeInterfaces {
   set net [e2_sll::driver_net [dict get $record driver]]
   if {[get_property NAME $net] ne [dict get $record net]} {error "Native DFX interface identity changed"}
   set status [get_property ROUTE_STATUS $net]
   set stub [regexp {\mSTUB\M} [get_property ROUTE $net]]
   set actual [lsort -unique [get_property NAME [get_nodes -of_objects $net -filter {IS_SLL == 1}]]]
   set planned [dict get $record planned_resources]
   set ok [expr {$status eq "ROUTED" && !$stub}]
   puts $f [join [list [dict get $record net] native-interface-fallback $status $stub [join $planned ,] [join $actual ,] [expr {$actual eq $planned}] $ok] \t]
   if {!$ok} {lappend failures [dict get $record net]}
  }
 } finally {close $f}
 if {[llength $failures]} {error "Native DFX interfaces not fully routed or have STUB: $failures"}
 puts "E2_DFX_NATIVE_INTERFACES_ROUTED [llength $nativeInterfaces]"
}
proc dfx_sll::full_audit_scope {} {
 variable fullScopeMode;variable fullNativeCount
 return [dict create constraint_scope $fullScopeMode native_interface_nets $fullNativeCount all_planned_positions_enforced [expr {!$fullNativeCount}]]
}
