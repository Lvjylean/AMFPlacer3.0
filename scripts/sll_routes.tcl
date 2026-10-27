# E2 directed SLL trees. Ordinary routing remains GAP-based and unconstrained.
namespace eval e2_sll {variable records {};variable pinNodes {};variable used {}}
proc e2_sll::terminal {name expectedSlr} {
 variable pinNodes
 if {[dict exists $pinNodes $name]} {return [dict get $pinNodes $name]}
 set pin [get_pins -hierarchical -filter "NAME == $name"]
 if {[llength $pin]!=1} {error "E2 missing or ambiguous pin $name"}
 set cell [get_cells -of_objects $pin]
 if {[get_property LOC $cell] eq "" || [get_property BEL $cell] eq ""} {error "E2 endpoint is unplaced: $name"}
 set slr [get_slrs -of_objects [get_sites [get_property LOC $cell]]]
 if {$slr ne "SLR$expectedSlr"} {error "E2 stale plan: $name is $slr, expected SLR$expectedSlr"}
 set nodes [lsort -unique [get_property NAME [get_nodes -of_objects [get_site_pins -of_objects $pin]]]]
 if {[llength $nodes]!=1} {error "E2 unsupported physical pin access for $name: $nodes"}
 dict set pinNodes $name $nodes
 return $nodes
}
proc e2_sll::children {items prefix loadsVar segmentsVar} {
 upvar 1 $loadsVar loads $segmentsVar segments
 set branches {}
 foreach child $items {
  if {[dict exists $child pin]} {
   set pin [dict get $child pin]
   if {[dict exists $loads $pin]} {error "E2 repeated receiver $pin"}
   dict set loads $pin $prefix
   lappend branches [list GAP [terminal $pin [dict get $child slr]]]
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
proc e2_sll::apply {planFile out} {
 variable records;variable pinNodes;variable used
 set records {};set pinNodes {};set used {}
 source $planFile
 set pending {};set cells {}
 # Validate every tree before changing routing properties.
 foreach record $e2_sll_plan {
  # AMF net identities are driver-pin names, not necessarily Vivado net names.
  set driver [dict get $record driver]
  set dp [get_pins -hierarchical -filter "NAME == $driver"]
  if {[llength $dp]!=1} {error "E2 missing driver pin $driver"}
  set net [get_nets -parent_net -segments -of_objects $dp]
  if {[llength $net]!=1} {error "E2 missing or ambiguous net for $driver"}
  set name [get_property NAME $net]
  if {[get_property IS_CONTAIN_ROUTING $net]} {error "E2 unexpected routing-region restriction"}
  set driver [dict get $record driver];set loads {};set segments {}
  set root [terminal $driver [dict get $record source_slr]]
  set tail [children [dict get $record tree] {} loads segments]
  set actual [lsort -unique [get_property NAME [get_pins -leaf -of_objects [get_nets -segments -of_objects $dp] -filter {DIRECTION == IN}]]]
  if {$actual ne [lsort [dict keys $loads]] || [get_property NAME [get_pins -leaf -of_objects [get_nets -segments -of_objects $dp] -filter {DIRECTION == OUT}]] ne $driver} {error "E2 net connectivity changed: $name"}
  dict for {resource segment} $segments {
   foreach n $segment {if {[dict exists $used $n]} {error "E2 selected routing resource collision $n"};dict set used $n $name}
  }
  foreach pin [concat [list $driver] [dict keys $loads]] {dict set cells [get_property NAME [get_cells -of_objects [get_pins $pin]]] 1}
  set route [concat [list $root] $tail]
  lappend pending [list $net $route]
  lappend records [dict create net $name driver $driver root $root loads $loads segments $segments]
 }
 foreach c [dict keys $cells] {
  set cell [get_cells -hierarchical -filter "NAME == $c"]
  set_property IS_LOC_FIXED true $cell;set_property IS_BEL_FIXED true $cell
  if {[string match LUT* [get_property REF_NAME $cell]]} {
   set mapping {}
   foreach pin [get_pins -of_objects $cell -filter {DIRECTION == IN}] {
    set sp [get_site_pins -of_objects $pin]
    if {[llength $sp]!=1} {error "E2 cannot preserve LUT input mapping $pin"}
    lappend mapping "[file tail $pin]:[file tail $sp]"
   }
   set_property LOCK_PINS $mapping $cell
  }
 }
 file mkdir $out
 set f [open $out/sll-fixed-route.xdc w]
 foreach item $pending {lassign $item net route;set_property FIXED_ROUTE $route $net;puts $f [list set_property FIXED_ROUTE $route $net]}
 close $f
 puts "E2_SLL_TREES_APPLIED [llength $records]"
}
proc e2_sll::audit {out} {
 variable records;variable pinNodes
 set f [open $out/sll-path-audit.tsv w];puts $f "net\tpin\texpected\tactual\tmatch"
 set count 0
 foreach record $records {
  set net [get_nets -hierarchical -filter "NAME == [dict get $record net]"]
  if {[get_property ROUTE_STATUS $net] ne "ROUTED" || [string match *STUB* [get_property ROUTE $net]]} {error "E2 incomplete routed tree $net"}
  set adj {};set routed [get_property NAME [get_nodes -of_objects $net]]
  foreach pip [get_pips -of_objects $net] {
   set a [get_property NAME [get_nodes -uphill -of_objects $pip]];set b [get_property NAME [get_nodes -downhill -of_objects $pip]]
   if {[llength $a]!=1 || [llength $b]!=1} {error "E2 ambiguous route graph $pip"}
   dict lappend adj $a $b
  }
  set root [dict get $record root];set queue [list $root];set head 0;set prev [dict create $root ""]
  while {$head<[llength $queue]} {set a [lindex $queue $head];incr head;if {![dict exists $adj $a]} {continue};foreach b [dict get $adj $a] {if {![dict exists $prev $b]} {dict set prev $b $a;lappend queue $b}}}
  set actualResources [lsort [get_property NAME [get_nodes -of_objects $net -filter {IS_SLL == 1}]]]
  if {$actualResources ne [lsort [dict keys [dict get $record segments]]]} {error "E2 extra or missing SLL on $net: $actualResources"}
  dict for {pin expected} [dict get $record loads] {
   set target [dict get $pinNodes $pin]
   if {![dict exists $prev $target]} {error "E2 receiver disconnected $pin"}
   set actual {};set cur $target
   while {$cur ne ""} {if {$cur in $actualResources} {set actual [linsert $actual 0 $cur]};set cur [dict get $prev $cur]}
   set match [expr {$actual eq $expected}]
   puts $f [join [list $net $pin [join $expected ,] [join $actual ,] $match] "\t"]
   if {!$match} {error "E2 receiver SLL path mismatch $pin: $actual vs $expected"}
   incr count
  }
 }
 close $f;puts "E2_SLL_PATHS_VERIFIED $count"
}
