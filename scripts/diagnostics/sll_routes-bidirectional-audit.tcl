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
proc e2_sll::apply {planFile out} {
 variable records;variable pinNodes;variable used;variable cellsByName;variable leafPinsByName;variable receiverNodePaths
 set records {};set pinNodes {};set used {}
 refresh_access_index
 source $planFile
 set pending {};set cells {}
 # Validate every tree before changing routing properties.
 foreach record $e2_sll_plan {
  set receiverNodePaths {}
  # AMF net identities are driver-pin names, not necessarily Vivado net names.
  set driver [dict get $record driver]
  set dp [pin_object $driver]
  set net [driver_net $driver]
  set name [get_property NAME $net]
  if {[get_property IS_CONTAIN_ROUTING $net]} {error "E2 unexpected routing-region restriction"}
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
   set remoteLoads {};set remoteSegments {}
   set route [children $crossings {} remoteLoads remoteSegments]
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
 foreach c [dict keys $cells] {
  set cell [dict get $cellsByName $c]
  set_property IS_LOC_FIXED true $cell;set_property IS_BEL_FIXED true $cell
  if {[string match LUT* [get_property REF_NAME $cell]]} {
   set mapping {}
   foreach pin [get_pins -of_objects $cell -filter {DIRECTION == IN}] {
    set bp [get_bel_pins -of_objects $pin]
    set physical [lsort -unique [lmap b $bp {file tail $b}]]
    if {[llength $physical]!=1} {error "E2 cannot preserve LUT input mapping $pin: $bp"}
    lappend mapping "[file tail $pin]:[lindex $physical 0]"
   }
   set_property LOCK_PINS $mapping $cell
  }
 }
 file mkdir $out
 set af [open $out/sll-macro-pin-aliases.tsv w];puts $af "logical_pin\tphysical_leaf_pins"
 dict for {name leaves} $leafPinsByName {set leafNames [get_property NAME $leaves];if {$leafNames ne $name} {puts $af [join [list $name $leafNames] "\t"]}};close $af
 set f [open $out/sll-fixed-route.xdc w]
 set sf [open $out/sll-source-selection.tsv w]
 puts $sf "net\tmode\tcandidate_outputs\timport_selected_outputs"
 set index 0
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
   dict set record import_selected_outputs $selected
   lset records $index $record
   puts $sf [join [list $net [dict get $record source_mode] $roots $selected] "\t"]
   puts $f [constraint_command $route [get_property NAME $net]]
   incr index
   if {$index%250==0} {puts "E2_SLL_TREES_APPLIED_PROGRESS $index";flush stdout}
  }
 } finally {close $f;close $sf}
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
proc e2_sll::audit {out} {
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
   # Diagnostic-only: a used bidirectional PIP is traversable both ways.
   # Do not invent reverse edges for directional PIPs or unused resources.
   if {![get_property IS_DIRECTIONAL $pip]} {
    if {[get_property IS_SLL [get_nodes -of_objects $pip]] ne {0 0}} {
     foreach endpoint [get_nodes -of_objects $pip] {
      if {[get_property IS_SLL $endpoint]} {error "Diagnostic requires explicit direction for an SLL-adjacent bidirectional PIP: $pip"}
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
  set roots {};foreach root $candidates {if {$root in $routed && [dict exists $adj $root]} {lappend roots $root}}
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
