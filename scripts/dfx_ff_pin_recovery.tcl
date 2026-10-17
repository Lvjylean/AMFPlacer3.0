# Opt-in recovery from an immutable, already imported joint checkpoint.
# AMF selects all locations from the extracted access certificate. No placer.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_joint_checks.tcl]
namespace eval ff_repair {
 proc one {objects label} {
  if {[llength $objects]!=1} {error "Expected one $label, got [llength $objects]"}
  return [lindex $objects 0]
 }
 proc pin_signature {c} {
  set result {}
  foreach p [get_pins -of_objects $c] {
   dict set result [get_property REF_PIN_NAME $p] [list [get_property DIRECTION $p] [amf_multi::names [get_nets -quiet -of_objects $p]]]
  }
  set properties [list_property $c]
  foreach k {REF_NAME INIT IS_C_INVERTED IS_D_INVERTED IS_R_INVERTED} {if {$k in $properties} {dict set result property_$k [get_property $k $c]}}
  return $result
 }
 proc half {b} {return [expr {[string first [string index $b 0] ABCDEFGH]/4}]}
 proc bank {b} {return [list [half $b] [expr {[string length $b]==4}]]}
 proc access {target expect} {
  global dfx
  set bp [one [get_bel_pins $target/Q] bel_Q]
  set pins [get_site_pins -of_objects $bp]
  if {![llength $pins]} {error "No direct Q site-pin mapping: $target"}
  set paths 0;set unavailable 0;set fixedRoutes {};set detail {}
  foreach sp $pins {
   set nodes [get_nodes -of_objects $sp]
   if {![llength $nodes]} {error "No Q external node: $sp"}
   foreach n $nodes {
    set srcOwners [get_nets -quiet -of_objects $n]
    if {$expect eq "free" && [llength $srcOwners]} {error "Target Q node already owned: $n"}
    foreach next [get_nodes -downhill -of_objects $n] {
     set intent [get_property INTENT_CODE $next]
     if {[get_property IS_GND $next] || [get_property IS_VCC $next]} {continue}
     if {[get_property IS_BAD $next]} {error "Bad first-hop node $next"}
     incr paths
     set owners [get_nets -quiet -of_objects $next]
     if {[llength $owners]} {incr unavailable}
     if {$expect eq "free" && [llength $owners]} {error "Target Q first hop owned: $next"}
     if {$expect eq "blocked"} {
      if {![llength $owners]} {error "Certificate blockage no longer holds at $next"}
      foreach net $owners {
       if {![get_property IS_ROUTE_FIXED $net]} {error "Blocked first-hop owner is not fixed: $net"}
       foreach segment [get_nets -segments $net] {
        if {[amf_multi::active [get_property NAME $segment]]} {error "Blocker belongs to an RP: $net"}
       }
       dict set fixedRoutes [get_property NAME $net] [list [get_property ROUTE $net] [get_property FIXED_ROUTE $net] [get_property IS_ROUTE_FIXED $net]]
      }
     }
     lappend detail [list [get_property NAME $sp] [get_property NAME $n] [get_property NAME $next] $intent [amf_multi::names $owners]]
    }
   }
  }
  if {!$paths} {error "No general-data Q first-hop options: $target"}
  set f [open $dfx(output)/repair_live_access.tcldata a]
  puts $f [list $target $expect $paths $unavailable $detail];close $f
  return $fixedRoutes
 }
 proc apply {requests} {
  global dfx
  set started [clock milliseconds];set changed 0;set old {}
  foreach row $dfx(rps) {
   lassign $row id rp pb
   set f [open [set dfx(delta_$id)] r]
   if {[gets $f] ne "cell\tfrom\tto"} {error "Malformed AMF placement delta"}
   while {[gets $f line]>=0} {
    if {$line eq ""} {continue}
    if {[llength [split $line \t]]!=3} {error "Malformed delta row"}
    lassign [split $line \t] name from to
    if {[dict exists $old $name] || [string first "$rp/" $name]!=0} {error "Duplicate or wrong-scope delta"}
    if {![dict exists $requests $id $name] || [dict get $requests $id $name] ne $to} {error "Delta/placement disagreement"}
    lassign [split $from /] site sourceBel;lassign [split $to /] targetSite targetBel
    if {$site ne $targetSite || $from eq $to || ![regexp {^[A-H]FF2?$} $sourceBel] || ![regexp {^[A-H]FF2?$} $targetBel] || [half $sourceBel]!=[half $targetBel]} {error "Repair exceeds same-site/same-half FF policy"}
    set c [one [get_cells -quiet -hierarchical -filter "NAME == \"$name\""] dynamic_cell]
    if {[get_property REF_NAME $c] ne "FDRE" || [get_property LOC $c] ne $site || [lindex [split [get_property BEL $c] .] end] ne $sourceBel} {error "Live source placement/type mismatch"}
    set target [one [get_bels $to] target_BEL]
    if {[llength [get_cells -quiet -of_objects $target]]} {error "Target BEL occupied"}
    if {[get_property PROHIBIT [get_sites $site]]} {error "Repair site prohibited"}
    if {[bank $sourceBel] ne [bank $targetBel]} {
     foreach other [get_cells -quiet -of_objects [get_sites $site]] {
      set b [lindex [split [get_property BEL $other] .] end]
      if {[regexp {^[A-H]FF2?$} $b] && [bank $b] eq [bank $targetBel]} {error "Target CE bank is not empty"}
     }
    }
    set before [pin_signature $c]
    set routes [access $from blocked]
    access $to free
    dict set old $name $from
    # This checkpoint contains no routed new RM. Static nets remain locked.
    unplace_cell $c
    place_cell [list $name $to]
    if {[get_property LOC $c] ne $site || [lindex [split [get_property BEL $c] .] end] ne $targetBel || [pin_signature $c] ne $before} {error "Live repair changed connectivity, properties, or failed placement"}
    dict for {n signature} $routes {
     set net [one [get_nets -quiet -hierarchical -filter "NAME == \"$n\""] static_blocker]
     if {[list [get_property ROUTE $net] [get_property FIXED_ROUTE $net] [get_property IS_ROUTE_FIXED $net]] ne $signature} {error "Repair changed fixed static route $n"}
    }
    incr changed
    if {([clock milliseconds]-$started)/1000.0+$dfx(repair_preprocessing_seconds)>$dfx(repair_budget_seconds)} {error "Added repair time exceeded budget"}
   }
   close $f
  }
  set total [expr {([clock milliseconds]-$started)/1000.0+$dfx(repair_preprocessing_seconds)}]
  if {$total>$dfx(repair_budget_seconds)} {error "Added repair time exceeded budget"}
  amf_dfx::save_dict $dfx(output)/repair_changes.tcldata $old
  set f [open $dfx(output)/repair_timing.tsv w];puts $f "changed_cells\t$changed";puts $f "added_repair_seconds\t$total";puts $f "budget_seconds\t$dfx(repair_budget_seconds)";close $f
  puts "AMF_FF_REPAIR_APPLIED changed=$changed seconds=$total"
 }
}
proc main {} {
 global dfx
 set_param general.maxThreads $dfx(vivado_threads)
 if {$dfx(vivado_threads)!=4 || $dfx(clock_period_ns)!=$dfx(experiment_clock_period_ns) || $dfx(static_routing_containment) || $dfx(backend_place_mode) ne "route-only" || $dfx(repair_budget_seconds)>180} {error "Unexpected recovery conditions"}
 set requests {};set boundaries {}
 foreach row $dfx(rps) {
  lassign $row id rp pb
  dict set requests $id [joint::requested $id $rp]
  dict set boundaries $id [amf_dfx::read_dict [file join $dfx(common_static_metadata) ${id}_common_static_boundary.tcldata]]
 }
 amf_dfx::timed open_existing_joint_imported {open_checkpoint $dfx(recovery_checkpoint)}
 set original_clocks [dfx_clock_contract::signature]
 amf_dfx::timed apply_ff_pin_repair {ff_repair::apply $requests}
 dfx_clock_contract::assert_unchanged $original_clocks after_pin_repair
 set footprints [amf_dfx::read_dict $dfx(imported_footprints)]
 amf_dfx::save_dict $dfx(output)/imported_rp_route_footprints.tcldata $footprints
 amf_dfx::timed audit_repaired {joint::audit repaired $requests $boundaries $footprints}
 amf_dfx::timed write_joint_repaired {write_checkpoint $dfx(output)/joint_amf_placed.dcp}
 set f [open $dfx(output)/vivado_placement_not_executed.txt w];puts $f {No place_design; AMF native repair, one same-site FF BEL change, followed by joint route};close $f
 amf_dfx::timed vivado_route {route_design}
 amf_dfx::timed write_joint_routed {write_checkpoint $dfx(output)/joint_routed.dcp}
 puts AMF_JOINT_ROUTE_COMPLETED
}
if {[catch {main} message opts]} {puts stderr $message;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
