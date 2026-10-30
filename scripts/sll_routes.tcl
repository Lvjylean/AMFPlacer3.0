# E2 directed SLL trees. Ordinary routing remains GAP-based and unconstrained.
namespace eval e2_sll {variable records {};variable pinNodes {};variable used {};variable cellsByName {};variable pinsByName {};variable leafPinsByName {};variable siteSlrs {};variable receiverNodePaths {};variable cellLocations {};variable cellBels {}}
proc e2_sll::refresh_access_index {} {
 variable cellsByName;variable pinsByName;variable leafPinsByName;variable siteSlrs;variable cellLocations;variable cellBels
 set cellsByName {};set pinsByName {};set leafPinsByName {};set siteSlrs {}
 set objects [get_cells -hierarchical -filter {IS_PRIMITIVE}]
 set cellLocations {};set cellBels {}
 foreach name [get_property NAME $objects] object $objects loc [get_property LOC $objects] bel [get_property BEL $objects] {
  dict set cellsByName $name $object;dict set cellLocations $name $loc;dict set cellBels $name $bel
 }
}
proc e2_sll::pin_object {name} {
 variable cellsByName;variable pinsByName
 if {[dict exists $pinsByName $name]} {return [dict get $pinsByName $name]}
 set owner [file dirname $name]
 if {![dict exists $cellsByName $owner]} {error "E2 missing endpoint cell $owner"}
 # A full-design hierarchical pin filter per receiver is prohibitively
 # expensive on GETRF. Filter only the indexed owning cell's pins.
 set pin [get_pins -of_objects [dict get $cellsByName $owner] -filter "NAME == $name"]
 if {[llength $pin]!=1} {error "E2 missing or ambiguous pin $name"}
 dict set pinsByName $name $pin
 return $pin
}
proc e2_sll::physical_pins {name} {
 variable leafPinsByName
 if {[dict exists $leafPinsByName $name]} {return [dict get $leafPinsByName $name]}
 set pin [pin_object $name]
 if {[get_property IS_LEAF $pin]} {dict set leafPinsByName $name $pin;return $pin}
 # Unisim transformation keeps an original macro interface with no site
 # pins (e.g. DSP/A[6]). Resolve its connected physical leaves INSIDE that
 # macro; never invent a DSP child name or select unrelated net receivers.
 set direction [get_property DIRECTION $pin]
 set leaves [get_pins -leaf -of_objects [get_nets -segments -of_objects $pin] -filter "DIRECTION == $direction"]
 set prefix "[file dirname $name]/"
 set resolved {}
 foreach leaf $leaves {
  if {[string first $prefix [get_property NAME $leaf]]==0} {lappend resolved $leaf}
 }
 if {![llength $resolved]} {error "E2 no connected physical leaves for macro pin $name"}
 dict set leafPinsByName $name $resolved
 return $resolved
}
proc e2_sll::driver_net {driver} {
 set net [get_nets -parent_net -segments -of_objects [pin_object $driver]]
 if {[llength $net]!=1} {error "E2 missing or ambiguous net for $driver"}
 return $net
}
proc e2_sll::validate_endpoint {name expectedSlr} {
 variable cellLocations;variable cellBels;variable siteSlrs
 set owner [file dirname $name]
 if {![dict exists $cellLocations $owner]} {error "E2 missing endpoint cell $owner"}
 set loc [dict get $cellLocations $owner]
 if {$loc eq "" || [dict get $cellBels $owner] eq ""} {error "E2 endpoint is unplaced: $name"}
 if {![dict exists $siteSlrs $loc]} {dict set siteSlrs $loc [get_slrs -of_objects [get_sites $loc]]}
 set slr [dict get $siteSlrs $loc]
 if {$slr ne "SLR$expectedSlr"} {error "E2 stale plan: $name is $slr, expected SLR$expectedSlr"}
}
proc e2_sll::terminal {name expectedSlr} {
 variable pinNodes
 validate_endpoint $name $expectedSlr
 if {[dict exists $pinNodes $name]} {return [dict get $pinNodes $name]}
 set mapped [get_nodes -quiet -of_objects [get_site_pins -quiet -of_objects [physical_pins $name]]]
 if {![llength $mapped]} {error "E2 no external physical pin access for $name"}
 set nodes [lsort -unique [get_property NAME $mapped]]
 # A single logical driver can reach several site output pins. Keep the set;
 # never choose an arbitrary output or confuse it with multiple logical nets.
 if {![llength $nodes]} {error "E2 no external physical pin access for $name"}
 dict set pinNodes $name $nodes
 return $nodes
}
proc e2_sll::children {items prefix loadsVar segmentsVar} {
 upvar 1 $loadsVar loads $segmentsVar segments
 variable receiverNodePaths
 set branches {};set emittedNodes {}
 foreach child $items {
  if {[dict exists $child pin]} {
   set pin [dict get $child pin]
   if {[dict exists $loads $pin]} {error "E2 repeated receiver $pin"}
   dict set loads $pin $prefix
   validate_endpoint $pin [dict get $child slr]
   # Local branches require no interconnect waypoint. They may be internal
   # CARRY/LUT-to-FF connections with no external site input at all. Keep
   # every logical receiver for connectivity and post-route path checks.
   if {![llength $prefix]} {continue}
   set nodes [terminal $pin [dict get $child slr]]
   # A logical receiver can drive several mapped physical inputs, e.g.
   # the lower and upper address pins of a RAMB36. They belong to the
   # SAME receiver group and must not be arbitrarily reduced to one pin.
   foreach node $nodes {
    if {[dict exists $receiverNodePaths $node] && [dict get $receiverNodePaths $node] ne $prefix} {error "E2 conflicting SLL groups for shared physical receiver node $node"}
    dict set receiverNodePaths $node $prefix
    # Several logical CE/SR pins can share one physical site input. A DRS
    # tree must contain that node once (otherwise Route 35-3477), while
    # every logical receiver remains in loads for the path audit.
    if {![dict exists $emittedNodes $node]} {lappend branches [list GAP $node];dict set emittedNodes $node 1}
   }
  } else {
   set segment [dict get $child segment]
   set resource [dict get $child resource]
   if {[llength $segment]!=5 || [lindex $segment 2] ne $resource} {error "E2 invalid SLL segment"}
   if {[dict exists $segments $resource]} {error "E2 repeated SLL crossing object $resource"}
   dict set segments $resource $segment
   set objects {}
   foreach name $segment {
    set node [get_nodes -of_objects [get_tiles [lindex [split $name /] 0]] -filter "NAME == $name"]
    if {[llength $node]!=1} {error "E2 nonexistent SLL node $name"}
    lappend objects $node
   }
   set from [get_slrs -of_objects [get_tiles -of_objects [lindex $objects 0]]]
   set to [get_slrs -of_objects [get_tiles -of_objects [lindex $objects end]]]
   if {$from ne "SLR[dict get $child from]" || $to ne "SLR[dict get $child to]"} {error "E2 incorrect SLL direction $resource"}
   foreach a [lrange $objects 0 end-1] b [lrange $objects 1 end] {
    if {$b ni [get_property NAME [get_nodes -downhill -of_objects $a]]} {error "E2 disconnected SLL segment $a -> $b"}
   }
   set sub [children [dict get $child children] [concat $prefix [list $resource]] loads segments]
   lappend branches [concat [list GAP] $segment $sub]
  }
 }
 if {![llength $branches]} {error "E2 empty receiver subtree"}
 set out {};foreach branch [lrange $branches 0 end-1] {lappend out $branch}
 return [concat $out [lindex $branches end]]
}
proc e2_sll::source_accesses {route candidates} {
 set found {}
 foreach item $route {
  if {$item in $candidates} {lappend found $item} elseif {[llength $item]>1} {
   set found [concat $found [source_accesses $item $candidates]]
  }
 }
 return [lsort -unique $found]
}
proc e2_sll::constraint_command {route netName} {
 # Object handles cannot be serialized as bare strings: some net names
 # resolve as invalid/ambiguous objects in set_property. Query the net
 # explicitly, with Tcl list quoting preserving brackets and spaces.
 return "set_property FIXED_ROUTE [list $route] \[get_nets [list $netName]\]"
}
proc e2_sll::needs_native_source_resolution {driver selected roots} {
 if {[llength $roots]<2 || [llength $selected]!=1} {return 0}
 set cell [get_cells [file dirname $driver]]
 if {[get_property REF_NAME $cell] ne "CARRY8"} {return 0}
 set root [lindex $selected 0]
 if {![string match *_COUT $root]} {return 0}
 set downhill [get_nodes -quiet -downhill -of_objects [get_nodes $root]]
 # A boundary COUT may have no downstream node at all. It still cannot
 # serve an SLL route; ask Vivado to resolve its alternative fabric access.
 if {![llength $downhill]} {return 1}
 set next [get_property NAME $downhill]
 foreach node $next {if {![string match *_CIN_PIN $node]} {return 0}}
 return 1
}
# A CARRY net may use COUT for a local CIN and another output for its SLL
# branches. Keep the output actually chosen by native routing for crossing;
# do not reject legitimate local roots or choose a named fabric alternative.
proc e2_sll::roots_reaching_sll {adj roots slls} {
 set result {}
 foreach root $roots {
  set queue [list $root];set head 0;set seen {};set reaches 0
  while {$head<[llength $queue]} {
   set node [lindex $queue $head];incr head
   if {[dict exists $seen $node]} {continue}
   dict set seen $node 1
   if {$node in $slls} {set reaches 1;break}
   if {[dict exists $adj $node]} {
    foreach next [dict get $adj $node] {if {![dict exists $seen $next]} {lappend queue $next}}
   }
  }
  if {$reaches} {lappend result $root}
 }
 return $result
}
proc e2_sll::native_crossing_source {net roots} {
 set selected [source_accesses [get_property ROUTE $net] $roots]
 if {[llength $selected]<=1} {return $selected}
 set sllObjects [get_nodes -quiet -of_objects $net -filter {IS_SLL == 1}]
 if {![llength $sllObjects]} {error "E2 native crossing route has no SLL on $net"}
 set adj {}
 foreach pip [get_pips -of_objects $net] {
  set a [get_property NAME [get_nodes -uphill -of_objects $pip]]
  set b [get_property NAME [get_nodes -downhill -of_objects $pip]]
  if {[llength $a]!=1 || [llength $b]!=1} {error "E2 ambiguous native source graph $pip"}
  dict lappend adj $a $b
  if {![get_property IS_DIRECTIONAL $pip]} {dict lappend adj $b $a}
 }
 set crossing [roots_reaching_sll $adj $selected [get_property NAME $sllObjects]]
 puts "E2_SLL_NATIVE_SOURCE_ROOTS all=$selected crossing=$crossing"
 return $crossing
}
proc e2_sll::native_source_route {net route roots} {
 # FIXED_ROUTE import can choose the dedicated carry COUT even when the
 # requested SLL requires fabric access. Ask Vivado's router to resolve
 # the source, then discard the probe routing and retain only its output.
 # AMF does not select a named alternative or freeze ordinary wire paths.
 set name [get_property NAME $net]
 set tail $route
 if {[lindex $tail 0] in $roots} {set tail [lrange $tail 1 end]}
 reset_property FIXED_ROUTE $net
 set rc [catch {
  # Resetting the property leaves import-selected routing in the route
  # database. Remove that stale COUT root before native source selection.
  route_design -unroute -nets [get_nets $name]
  route_design -nets [get_nets $name]
  set selected [native_crossing_source [get_nets $name] $roots]
  if {[llength $selected]!=1} {error "E2 native router did not resolve one source output on $name: $selected"}
 } message options]
 set clearRC [catch {route_design -unroute -nets [get_nets $name]} clearMessage clearOptions]
 if {$rc} {return -options $options $message}
 if {$clearRC} {return -options $clearOptions $clearMessage}
 puts "E2_SLL_NATIVE_SOURCE_RESOLVED $name $selected"
 return [concat $selected $tail]
}
# Resolve exceptional CARRY outputs together. Native routing only selects the
# physical source; all probe routing is removed and the original SLL tree is
# retained. Each net's own roots/route are used, including local carry roots.
proc e2_sll::native_source_routes {requests} {
 if {![llength $requests]} {return {}}
 set names {};set seen {}
 foreach request $requests {
  lassign $request name route roots
  if {[dict exists $seen $name]} {error "E2 repeated native source request $name"}
  dict set seen $name 1;lappend names $name
 }
 set nets [get_nets $names]
 if {[llength $nets]!=[llength $names]} {error "E2 native source batch coverage mismatch"}
 reset_property FIXED_ROUTE $nets
 set resolved {}
 set rc [catch {
  route_design -unroute -nets $nets
  route_design -nets [get_nets $names]
  foreach request $requests {
   lassign $request name route roots
   set selected [native_crossing_source [get_nets [list $name]] $roots]
   if {[llength $selected]!=1} {error "E2 native router did not resolve one source output on $name: $selected"}
   if {[lindex $route 0] in $roots} {set route [lrange $route 1 end]}
   dict set resolved $name [concat $selected $route]
   puts "E2_SLL_NATIVE_SOURCE_RESOLVED $name $selected"
  }
 } message options]
 # Even a failed or partially completed batch must release its probe routes.
 set clearRC [catch {route_design -unroute -nets [get_nets $names]} clearMessage clearOptions]
 if {$rc} {return -options $options $message}
 if {$clearRC} {return -options $clearOptions $clearMessage}
 puts "E2_SLL_NATIVE_SOURCE_BATCH nets=[llength $names]"
 return $resolved
}
proc e2_sll::apply {planFile out} {
 variable records;variable pinNodes;variable used;variable cellsByName;variable leafPinsByName;variable receiverNodePaths
 variable cellLocations;variable cellBels
 set applyStarted [clock microseconds];set stageStarted $applyStarted;set stageTimes {}
 set records {};set pinNodes {};set used {}
 refresh_access_index
 source -notrace $planFile
 dict set stageTimes index_and_plan [expr {([clock microseconds]-$stageStarted)/1e6}]
 set stageStarted [clock microseconds]
 variable exportLocks;set exportLocks {}
 set pending {};set cells {}
 # Validate every tree before changing routing properties.
 foreach record $e2_sll_plan {
  set receiverNodePaths {}
  # AMF net identities are driver-pin names, not necessarily Vivado net names.
  set driver [dict get $record driver]
  set dp [pin_object $driver]
  set net [driver_net $driver]
  set name [get_property NAME $net]
  if {[get_property IS_CONTAIN_ROUTING $net] && !([info exists ::e2_sll::allowDfxRouting] && $::e2_sll::allowDfxRouting)} {error "E2 unexpected routing-region restriction"}
  set driver [dict get $record driver];set loads {};set segments {}
  set roots [terminal $driver [dict get $record source_slr]]
  set tail [children [dict get $record tree] {} loads segments]
  set actual [lsort -unique [get_property NAME [get_pins -leaf -of_objects [get_nets -segments -of_objects $dp] -filter {DIRECTION == IN}]]]
  set expectedLeaves {}
  dict for {logical path} $loads {
   foreach leaf [get_property NAME [physical_pins $logical]] {
    if {[dict exists $expectedLeaves $leaf] && [dict get $expectedLeaves $leaf] ne $path} {error "E2 conflicting SLL groups for shared physical leaf $leaf"}
    dict set expectedLeaves $leaf $path
   }
  }
  set actualDrivers [lsort -unique [get_property NAME [get_pins -leaf -of_objects [get_nets -segments -of_objects $dp] -filter {DIRECTION == OUT}]]]
  set expectedDrivers [lsort -unique [get_property NAME [physical_pins $driver]]]
  if {$actual ne [lsort [dict keys $expectedLeaves]] || $actualDrivers ne $expectedDrivers} {error "E2 net connectivity changed: $name"}
  dict for {resource segment} $segments {
   foreach n $segment {if {[dict exists $used $n]} {error "E2 selected routing resource collision $n"};dict set used $n $name}
  }
  foreach pin [concat [list $driver] [dict keys $loads]] {dict set cells [file dirname $pin] 1}
  set preview {}
  if {[llength $roots]==1} {
   set route [concat $roots $tail]
   set rootMode unique_physical_access
  } else {
   # Let Vivado resolve source access when importing a single subtree.
   # This is import-time choice, not an OR constraint kept until routing.
   # Local receivers stay unconstrained but remain in the path audit.
   # A GAP Steiner point is invalid (Route 35-562); LOGICAL_DRIVER is not
   # an accepted placeholder for unresolved branches in FIXED_ROUTE.
   set crossings {}
   foreach child [dict get $record tree] {if {[dict exists $child segment]} {lappend crossings $child}}
   if {![llength $crossings]} {error "E2 SLL plan contains no source crossing subtree: $driver"}
   # children already omits local waypoints at an empty prefix. Reuse the
   # validated tail instead of resolving every remote endpoint a second time.
   set route $tail
   if {[llength $crossings]==1} {
    set rootMode vivado_implicit_source
   } else {
    set previewLoads {};set previewSegments {}
    set preview [children [list [lindex $crossings 0]] {} previewLoads previewSegments]
    set rootMode vivado_import_selected_source
   }
  }
  lappend pending [list $net $route $preview $roots]
  lappend records [dict create net $name driver $driver source_slr [dict get $record source_slr] roots $roots source_mode $rootMode loads $loads segments $segments]
  if {[llength $records]%250==0} {puts "E2_SLL_TREES_VALIDATED [llength $records]";flush stdout}
 }
 dict set stageTimes validate_trees [expr {([clock microseconds]-$stageStarted)/1e6}]
 set stageStarted [clock microseconds]
 set endpointObjects {}
 foreach c [dict keys $cells] {lappend endpointObjects [dict get $cellsByName $c]}
 if {[llength $endpointObjects]} {
  set_property IS_LOC_FIXED true $endpointObjects
  set_property IS_BEL_FIXED true $endpointObjects
  set endpointTypes [get_property REF_NAME $endpointObjects]
 } else {set endpointTypes {}}
 foreach c [dict keys $cells] ref $endpointTypes {
  set cell [dict get $cellsByName $c]
  set mapping {}
  if {[string match LUT* $ref]} {
   set mapping {}
   foreach pin [get_pins -of_objects $cell -filter {DIRECTION == IN}] {
    set bp [get_bel_pins -of_objects $pin]
    set physical [lsort -unique [lmap b $bp {file tail $b}]]
    if {[llength $physical]!=1} {error "E2 cannot preserve LUT input mapping $pin: $bp"}
    lappend mapping "[file tail $pin]:[lindex $physical 0]"
   }
   set_property LOCK_PINS $mapping $cell
  }
  dict set exportLocks $c [list [dict get $cellLocations $c] [dict get $cellBels $c] $mapping]
 }
 dict set stageTimes lock_endpoints [expr {([clock microseconds]-$stageStarted)/1e6}]
 set stageStarted [clock microseconds]
 file mkdir $out
 set af [open $out/sll-macro-pin-aliases.tsv w];puts $af "logical_pin\tphysical_leaf_pins"
 dict for {name leaves} $leafPinsByName {set leafNames [get_property NAME $leaves];if {$leafNames ne $name} {puts $af [join [list $name $leafNames] "\t"]}};close $af
 set f [open $out/sll-fixed-route.xdc w]
 set sf [open $out/sll-source-selection.tsv w]
 puts $sf "net\tmode\tcandidate_outputs\timport_selected_outputs"
 set index 0;set nativeRequests {};set finalRoutes {}
 try {
  foreach item $pending {
   lassign $item net route preview roots
   if {[llength $preview]} {
    # The native normalizer selects the output; AMF never picks the first
    # candidate. The same source then anchors all specified SLL subtrees.
    set_property FIXED_ROUTE $preview $net
    set selected [source_accesses [get_property FIXED_ROUTE $net] $roots]
    if {[llength $selected]!=1} {error "E2 Vivado did not resolve one source access for $net: $selected"}
    set route [concat $selected $route]
   }
   set_property FIXED_ROUTE $route $net
   set selected [source_accesses [get_property FIXED_ROUTE $net] $roots]
   if {![llength $selected]} {error "E2 no native source access in normalized FIXED_ROUTE for $net"}
   set record [lindex $records $index]
   if {[needs_native_source_resolution [dict get $record driver] $selected $roots]} {
    lappend nativeRequests [list [dict get $record net] $route $roots]
   }
   dict set record import_selected_outputs $selected
   lset records $index $record
   dict set finalRoutes [dict get $record net] $route
   incr index
   if {$index%250==0} {puts "E2_SLL_TREES_APPLIED_PROGRESS $index";flush stdout}
  }
  dict set stageTimes import_trees [expr {([clock microseconds]-$stageStarted)/1e6}]
  set stageStarted [clock microseconds]
  set resolved [native_source_routes $nativeRequests]
  set index 0
  foreach record $records {
   set name [dict get $record net];set roots [dict get $record roots]
   if {[dict exists $resolved $name]} {
    set route [dict get $resolved $name];set net [get_nets [list $name]]
    set_property FIXED_ROUTE $route $net
    set selected [source_accesses [get_property FIXED_ROUTE $net] $roots]
    if {[llength $selected]!=1 || [needs_native_source_resolution [dict get $record driver] $selected $roots]} {
     error "E2 native source resolution was not retained for $name"
    }
    dict set finalRoutes $name $route
    dict set record source_mode vivado_native_router_source
    dict set record import_selected_outputs $selected
    lset records $index $record
   }
   incr index
  }
  dict set stageTimes native_source_batch [expr {([clock microseconds]-$stageStarted)/1e6}]
  set stageStarted [clock microseconds]
  foreach record $records {
   set name [dict get $record net]
   puts $sf [join [list $name [dict get $record source_mode] [dict get $record roots] [dict get $record import_selected_outputs]] "\t"]
   puts $f [constraint_command [dict get $finalRoutes $name] $name]
  }
 } finally {close $f;close $sf}
 dict set stageTimes write_constraints [expr {([clock microseconds]-$stageStarted)/1e6}]
 dict set stageTimes total [expr {([clock microseconds]-$applyStarted)/1e6}]
 set tf [open $out/sll-generation-timings.tsv w];puts $tf "phase\tseconds"
 dict for {stage seconds} $stageTimes {puts $tf "$stage\t$seconds"};close $tf
 puts "E2_SLL_TREES_APPLIED [llength $records]"
}
proc e2_sll::trace_paths {adj roots slls} {
 # Start at every actually used output of the ONE logical driver. If paths
 # reconverge, all witnesses must have the same SLL sequence: first-hit BFS
 # alone could otherwise hide a wrongly assigned receiver branch.
 set queue $roots;set head 0;set paths {}
 foreach root $roots {dict set paths $root {}}
 while {$head<[llength $queue]} {
  set a [lindex $queue $head];incr head
  if {![dict exists $adj $a]} {continue}
  foreach b [dict get $adj $a] {
   set path [dict get $paths $a]
   if {$b in $slls} {lappend path $b}
   if {[dict exists $paths $b]} {
    if {[dict get $paths $b] ne $path} {error "E2 conflicting SLL paths reach $b"}
   } else {dict set paths $b $path;lappend queue $b}
  }
 }
 return $paths
}
proc e2_sll::internal_receiver {driver receiver net} {
 # Called only after ROUTED/no-STUB was checked and no receiver site input
 # exists. Same-site BEL pins on the same logical net prove this receiver
 # uses the site's internal connection, rather than an SLL excursion.
 variable cellLocations
 set loc [dict get $cellLocations [file dirname $receiver]]
 if {$loc ne [dict get $cellLocations [file dirname $driver]]} {error "E2 receiver has no external access outside driver site: $receiver"}
 set bp [get_bel_pins -quiet -of_objects [physical_pins $receiver]]
 if {![llength $bp]} {error "E2 receiver has no physical BEL pin: $receiver"}
 set netBels [get_property NAME [get_bel_pins -of_objects $net]]
 foreach b $bp {if {$b ni $netBels} {error "E2 receiver BEL pin is not mapped on routed net: $b"}}
 if {[driver_net $receiver] ne $net} {error "E2 internal receiver net changed: $receiver"}
 return {}
}
proc e2_sll::audit_reference {out} {
 variable records;variable pinNodes
 # Physical optimization or checkpoint reload can invalidate object handles.
 refresh_access_index
 set f [open $out/sll-path-audit.tsv w];puts $f "net\tpin\texpected\tactual\tmatch"
 set rf [open $out/sll-source-access-audit.tsv w];puts $rf "net\tcandidate_outputs\tused_outputs"
 set count 0
 try {
 foreach record $records {
  set net [driver_net [dict get $record driver]]
  if {[get_property NAME $net] ne [dict get $record net]} {error "E2 net identity changed"}
  if {[get_property ROUTE_STATUS $net] ne "ROUTED" || [string match *STUB* [get_property ROUTE $net]]} {error "E2 incomplete routed tree $net"}
  set adj {};set routed [get_property NAME [get_nodes -of_objects $net]]
  foreach pip [get_pips -of_objects $net] {
   set a [get_property NAME [get_nodes -uphill -of_objects $pip]];set b [get_property NAME [get_nodes -downhill -of_objects $pip]]
   if {[llength $a]!=1 || [llength $b]!=1} {error "E2 ambiguous route graph $pip"}
   dict lappend adj $a $b
   # A used bidirectional PIP is traversable both ways.
   # Do not invent reverse edges for directional PIPs or unused resources.
   if {![get_property IS_DIRECTIONAL $pip]} {
    if {[get_property IS_SLL [get_nodes -of_objects $pip]] ne {0 0}} {
     foreach endpoint [get_nodes -of_objects $pip] {
      if {[get_property IS_SLL $endpoint]} {error "Audit requires explicit direction for an SLL-adjacent bidirectional PIP: $pip"}
     }
    }
    dict lappend adj $b $a
   }
  }
  # Requery after routing: Vivado may choose another site output. Merely
  # checking IS_USED on a site pin does not prove it is an interconnect root.
  set driver [dict get $record driver]
  set dp [pin_object $driver]
  set candidates [lsort -unique [get_property NAME [get_nodes -of_objects [get_site_pins -of_objects [physical_pins $driver]]]]]
  # A dedicated local COUT-to-CIN connection can be one used node with
  # no outgoing interconnect PIP. Include roots explicitly present in
  # ROUTE, even when their only receiver shares that same node.
  set usedOutputs [source_accesses [get_property ROUTE $net] $candidates]
  set roots {};foreach root $usedOutputs {if {$root in $routed} {lappend roots $root}}
  if {![llength $roots]} {error "E2 no routed physical output for $driver"}
  puts $rf [join [list $net $candidates $roots] "\t"]
  set actualResources [lsort [get_property NAME [get_nodes -of_objects $net -filter {IS_SLL == 1}]]]
  if {$actualResources ne [lsort [dict keys [dict get $record segments]]]} {error "E2 extra or missing SLL on $net: $actualResources"}
  set paths [trace_paths $adj $roots $actualResources]
  dict for {pin expected} [dict get $record loads] {
   set actual {};set witnesses 0
   set targetObjects [get_nodes -quiet -of_objects [get_site_pins -quiet -of_objects [physical_pins $pin]]]
   set targets {}
   if {[llength $targetObjects]} {set targets [get_property NAME $targetObjects]} else {
    set actual [internal_receiver $driver $pin $net];set witnesses 1
   }
   foreach target $targets {
    if {![dict exists $paths $target]} {continue}
    set candidate [dict get $paths $target]
    if {$witnesses && $actual ne $candidate} {error "E2 inconsistent receiver access paths $pin"}
    set actual $candidate;incr witnesses
   }
   if {!$witnesses} {error "E2 receiver disconnected $pin"}
   set match [expr {$actual eq $expected}]
   puts $f [join [list $net $pin [join $expected ,] [join $actual ,] $match] "\t"]
   if {!$match} {error "E2 receiver SLL path mismatch $pin: $actual vs $expected"}
   incr count
   if {$count%10000==0} {puts "READ_ONLY_AUDIT_PROGRESS $count";flush stdout}
  }
 }
 } finally {close $f;close $rf}
 puts "E2_SLL_PATHS_VERIFIED $count"
}

# Read-only audit helpers. Every cache is rebuilt from the current checkpoint.
proc e2_sll::prepare_audit_pins {} {
 variable records;variable cellsByName;variable pinsByName;variable leafPinsByName
 set needed {};set owners {}
 foreach record $records {
  foreach pin [concat [list [dict get $record driver]] [dict keys [dict get $record loads]]] {
   dict set needed $pin 1;dict set owners [file dirname $pin] 1
  }
 }
 set objects {}
 foreach owner [dict keys $owners] {
  if {![dict exists $cellsByName $owner]} {error "E2 missing endpoint cell $owner"}
  lappend objects [dict get $cellsByName $owner]
 }
 for {set offset 0} {$offset<[llength $objects]} {incr offset 2048} {
  set pins [get_pins -of_objects [lrange $objects $offset [expr {$offset+2047}]]]
  set names [get_property NAME $pins];set leaves [get_property IS_LEAF $pins]
  if {[llength $pins]!=[llength $names] || [llength $pins]!=[llength $leaves]} {error "E2 incomplete batch pin metadata"}
  foreach pin $pins name $names leaf $leaves {
   if {![dict exists $needed $name]} {continue}
   dict set pinsByName $name $pin
   if {$leaf} {dict set leafPinsByName $name $pin}
  }
 }
 foreach name [dict keys $needed] {
  if {![dict exists $pinsByName $name]} {error "E2 missing or ambiguous pin $name"}
 }
 return [dict size $needed]
}
proc e2_sll::audit_graph {net} {
 set adj {};set pips [get_pips -of_objects $net]
 if {![llength $pips]} {return $adj}
 # Property collections preserve the PIP collection's order. Batch this
 # query rather than crossing the Tcl/Vivado interface once per edge.
 set directions [get_property IS_DIRECTIONAL $pips]
 if {[llength $directions]!=[llength $pips]} {error "E2 incomplete PIP direction metadata"}
 foreach pip $pips directional $directions {
  set upstream [get_nodes -uphill -of_objects $pip]
  set downstream [get_nodes -downhill -of_objects $pip]
  if {[llength $upstream]!=1 || [llength $downstream]!=1} {error "E2 ambiguous route graph $pip"}
  # Node object string representations are their canonical names. Avoid
  # two redundant scalar NAME queries per PIP; topology queries stay native.
  set a [lindex $upstream 0];set b [lindex $downstream 0]
  dict lappend adj $a $b
  if {!$directional} {
   foreach endpoint [get_nodes -of_objects $pip] {
    if {[get_property IS_SLL $endpoint]} {error "Audit requires explicit direction for an SLL-adjacent bidirectional PIP: $pip"}
   }
   dict lappend adj $b $a
  }
 }
 return $adj
}

proc e2_sll::audit {out} {
 variable records;variable pinNodes
 # Physical optimization or checkpoint reload can invalidate object handles.
 set auditStarted [clock microseconds];set phaseStarted $auditStarted;set timing {}
 refresh_access_index
 set endpoints [prepare_audit_pins]
 dict set timing index [expr {([clock microseconds]-$phaseStarted)/1e6}]
 foreach phase {graph source receivers} {dict set timing $phase 0.0}
 set f [open $out/sll-path-audit.tsv w];puts $f "net\tpin\texpected\tactual\tmatch"
 set rf [open $out/sll-source-access-audit.tsv w];puts $rf "net\tcandidate_outputs\tused_outputs"
 set count 0
 try {
 foreach record $records {
  set net [driver_net [dict get $record driver]]
  if {[get_property NAME $net] ne [dict get $record net]} {error "E2 net identity changed"}
  if {[get_property ROUTE_STATUS $net] ne "ROUTED" || [string match *STUB* [get_property ROUTE $net]]} {error "E2 incomplete routed tree $net"}
  set phaseStarted [clock microseconds]
  set routed [get_property NAME [get_nodes -of_objects $net]]
  set adj [audit_graph $net]
  dict set timing graph [expr {[dict get $timing graph]+([clock microseconds]-$phaseStarted)/1e6}]
  set phaseStarted [clock microseconds]
  # Requery after routing: Vivado may choose another site output. Merely
  # checking IS_USED on a site pin does not prove it is an interconnect root.
  set driver [dict get $record driver]
  set dp [pin_object $driver]
  set candidates [lsort -unique [get_property NAME [get_nodes -of_objects [get_site_pins -of_objects [physical_pins $driver]]]]]
  # A dedicated local COUT-to-CIN connection can be one used node with
  # no outgoing interconnect PIP. Include roots explicitly present in
  # ROUTE, even when their only receiver shares that same node.
  set usedOutputs [source_accesses [get_property ROUTE $net] $candidates]
  set roots {};foreach root $usedOutputs {if {$root in $routed} {lappend roots $root}}
  if {![llength $roots]} {error "E2 no routed physical output for $driver"}
  puts $rf [join [list $net $candidates $roots] "\t"]
  set actualResources [lsort [get_property NAME [get_nodes -of_objects $net -filter {IS_SLL == 1}]]]
  if {$actualResources ne [lsort [dict keys [dict get $record segments]]]} {error "E2 extra or missing SLL on $net: $actualResources"}
  set paths [trace_paths $adj $roots $actualResources]
  dict set timing source [expr {[dict get $timing source]+([clock microseconds]-$phaseStarted)/1e6}]
  set phaseStarted [clock microseconds]
  dict for {pin expected} [dict get $record loads] {
   set actual {};set witnesses 0
   set targetObjects [get_nodes -quiet -of_objects [get_site_pins -quiet -of_objects [physical_pins $pin]]]
   set targets {}
   if {[llength $targetObjects]} {set targets [get_property NAME $targetObjects]} else {
    set actual [internal_receiver $driver $pin $net];set witnesses 1
   }
   foreach target $targets {
    if {![dict exists $paths $target]} {continue}
    set candidate [dict get $paths $target]
    if {$witnesses && $actual ne $candidate} {error "E2 inconsistent receiver access paths $pin"}
    set actual $candidate;incr witnesses
   }
   if {!$witnesses} {error "E2 receiver disconnected $pin"}
   set match [expr {$actual eq $expected}]
   puts $f [join [list $net $pin [join $expected ,] [join $actual ,] $match] "\t"]
   if {!$match} {error "E2 receiver SLL path mismatch $pin: $actual vs $expected"}
   incr count
   if {$count%10000==0} {puts "READ_ONLY_AUDIT_PROGRESS $count";flush stdout}
  }
  dict set timing receivers [expr {[dict get $timing receivers]+([clock microseconds]-$phaseStarted)/1e6}]
 }
 } finally {close $f;close $rf}
 dict set timing total [expr {([clock microseconds]-$auditStarted)/1e6}]
 set tf [open [file join $out sll-audit-timings.tsv] w];puts $tf "phase\tseconds"
 dict for {phase seconds} $timing {puts $tf "$phase\t$seconds"};close $tf
 puts "E2_SLL_AUDIT_BATCHED endpoints=$endpoints seconds=[dict get $timing total]"
 puts "E2_SLL_PATHS_VERIFIED $count"
}
