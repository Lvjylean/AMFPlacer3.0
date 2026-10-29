# Experimental DFX interface tree adapter; NOT enabled by the production flow.
# Simple input/output samples pass, but the mixed high-fanout sample has no
# validated constrained solution. See dfx-sll-stub-root-cause-20261029.md.
# A complete static tree fixes ordinary static routing as well as the SLL.
# Dynamic endpoint gaps remain free. Every result still needs Vivado route
# legality, per-receiver SLL auditing and static-shell preservation checks.
namespace eval e2_sll::dfx_bridge {variable ppByLeaf {};variable bridgeLog {}}
proc e2_sll::dfx_bridge::join_branches {branches} {
 if {![llength $branches]} {error "Empty DFX SLL receiver subtree"}
 return [concat [lrange $branches 0 end-1] [lindex $branches end]]
}
proc e2_sll::dfx_bridge::uncached_node {name} {
 set objects [get_nodes -of_objects [get_tiles [lindex [split $name /] 0]] -filter "NAME == $name"]
 if {[llength $objects]!=1} {error "Missing or ambiguous DFX bridge node: $name"}
 return $objects
}
proc e2_sll::dfx_bridge::connect {from to} {
 variable bridgeLog
 if {$from eq $to} {return [list $from]}
 set path [find_routing_path -from [node $from] -to [node $to] -max_nodes 1000]
 if {![llength $path] || [string match -nocase *no*path*found* $path]} {error "No free DFX SLL bridge: $from -> $to"}
 if {[lindex $path 0] ne $from || [lindex $path end] ne $to} {error "Invalid bridge endpoints: $from -> $to"}
 foreach node $path {
  if {[get_property IS_SLL [node $node]]} {error "DFX bridge introduces an unplanned SLL: $node"}
 }
 lappend bridgeLog [dict create from $from to $to nodes $path]
 return $path
}
proc e2_sll::dfx_bridge::children {items upstream prefix {sourcePP {}}} {
 variable ppByLeaf
 set branches {};set groups {};set emitted {}
 foreach item $items {
  if {[dict exists $item pin]} {
   # Local connections keep the same unconstrained treatment as e2_sll::children.
   if {![llength $prefix]} {continue}
   set pin [dict get $item pin]
   set nodes [e2_sll::terminal $pin [dict get $item slr]]
   foreach node $nodes {
    if {[dict exists $emitted $node]} {continue}
    dict set emitted $node 1
    if {[dict exists $ppByLeaf $pin]} {
     dict lappend groups [dict get $ppByLeaf $pin] $node
    } else {lappend branches [list GAP $node]}
   }
  } else {
   set segment [dict get $item segment]
   set nextPrefix [concat $prefix [list [dict get $item resource]]]
   set subtree [children [dict get $item children] [lindex $segment end] $nextPrefix]
   set head GAP
   if {$sourcePP ne {}} {set head [lrange [connect $sourcePP [lindex $segment 0]] 1 end-1]}
   lappend branches [concat $head $segment $subtree]
  }
 }
 dict for {pp nodes} $groups {
  set bridge [lrange [connect $upstream $pp] 1 end]
  set leaves {};foreach node $nodes {lappend leaves [list GAP $node]}
  lappend branches [concat $bridge [join_branches $leaves]]
 }
 return [join_branches $branches]
}
proc e2_sll::dfx_bridge::route {plan record ppMapping sourcePP} {
 # Diagnostic one-sided bridge only; not a general DFX interface fix.
 variable ppByLeaf;variable bridgeLog
 set ppByLeaf $ppMapping;set bridgeLog {}
 set roots [dict get $record import_selected_outputs]
 if {[llength $roots]!=1} {error "DFX bridge requires the import-selected crossing source"}
 set head $roots
 if {$sourcePP ne {}} {set head [concat $head GAP [list $sourcePP]]}
 return [concat $head [children [dict get $plan tree] $sourcePP {} $sourcePP]]
}
# Complete the static portion as an actual tree. Independent GAP branches on a
# DFX interface may be detached by partition routing even if each SLL is fixed.
namespace eval e2_sll::dfx_bridge {
 variable parents {};variable prefixes {};variable ends {};variable sourceAnchor {}
 variable dynamicLeaves {};variable nodeCache {};variable activeChildren {}
}
proc e2_sll::dfx_bridge::node {name} {
 variable nodeCache
 if {![dict exists $nodeCache $name]} {dict set nodeCache $name [uncached_node $name]}
 return [dict get $nodeCache $name]
}
proc e2_sll::dfx_bridge::insert {path prefix {resource {}}} {
 variable parents;variable prefixes
 set previous [lindex $path 0]
 if {![dict exists $prefixes $previous] || [dict get $prefixes $previous] ne $prefix} {error "Static tree prefix mismatch at $previous"}
 foreach current [lrange $path 1 end] {
  if {$current eq $resource} {lappend prefix $resource}
  if {[dict exists $prefixes $current]} {
   if {[dict get $prefixes $current] ne $prefix} {error "Different SLL groups merge at $current"}
   # Reuse the existing path from the root. Newly created dangling approaches
   # are pruned later; no second parent or directed cycle is introduced.
  } else {
   dict set parents $current $previous
   dict set prefixes $current $prefix
  }
  set previous $current
 }
 return $prefix
}
proc e2_sll::dfx_bridge::complete_connection {from to prefix} {
 variable prefixes;variable bridgeLog
 if {[dict exists $prefixes $to]} {
  if {[dict get $prefixes $to] ne $prefix} {error "Endpoint belongs to another SLL group: $to"}
  return
 }
 set excludes {}
 dict for {existing value} $prefixes {if {$value ne $prefix} {lappend excludes [node $existing]}}
 if {[llength $bridgeLog]%25==0} {puts "DFX_STATIC_CONNECT index=[llength $bridgeLog] from=$from to=$to";flush stdout}
 set clean 0
 for {set attempt 0} {$attempt<16} {incr attempt} {
  set args [list -from [node $from] -to [node $to] -max_nodes 256]
  if {[llength $excludes]} {lappend args -exclude_nodes $excludes}
  set path [find_routing_path {*}$args]
  if {![llength $path] || [string match -nocase *no*path*found* $path]} {error "No free static route: $from -> $to"}
  if {[lindex $path 0] ne $from || [lindex $path end] ne $to} {error "Invalid static route endpoints"}
  set blocked {}
  foreach n [lrange $path 1 end-1] {
   set object [node $n]
   # In a placed/partially routed design an unused site input may already
   # belong to another logical signal. It is not a free transit wire.
   if {[get_property IS_PIN $object] || [get_property IS_CLOCK $object] || [get_property IS_SLL $object]} {lappend blocked $object}
  }
  if {![llength $blocked]} {set clean 1;break}
  set excludes [lsort -unique [concat $excludes $blocked]]
 }
 if {!$clean} {error "No pin-safe static route within retry budget: $from -> $to"}
 foreach n $path {if {[get_property IS_SLL [node $n]]} {error "Static connector introduces unplanned SLL $n"}}
 insert $path $prefix
 lappend bridgeLog [dict create from $from to $to nodes $path]
}
proc e2_sll::dfx_bridge::complete_children {items upstream prefix} {
 variable ppByLeaf;variable ends;variable dynamicLeaves
 foreach item $items {
  if {[dict exists $item pin]} {
   if {![llength $prefix]} {continue}
   set pin [dict get $item pin]
   set terminals [e2_sll::terminal $pin [dict get $item slr]]
   if {[dict exists $ppByLeaf $pin]} {
    set pp [dict get $ppByLeaf $pin]
    complete_connection $upstream $pp $prefix
    dict set ends $pp 1
    foreach terminal $terminals {dict set dynamicLeaves $pp $terminal 1}
   } else {
    foreach terminal $terminals {
     complete_connection $upstream $terminal $prefix
     dict set ends $terminal 1
    }
   }
  } else {
   set segment [dict get $item segment]
   complete_connection $upstream [lindex $segment 0] $prefix
   set nextPrefix [insert $segment $prefix [dict get $item resource]]
   complete_children [dict get $item children] [lindex $segment end] $nextPrefix
  }
 }
}
proc e2_sll::dfx_bridge::emit {root} {
 variable activeChildren;variable dynamicLeaves
 set branches {}
 if {[dict exists $activeChildren $root]} {
  foreach child [dict get $activeChildren $root] {lappend branches [emit $child]}
 }
 if {[dict exists $dynamicLeaves $root]} {
  foreach terminal [dict keys [dict get $dynamicLeaves $root]] {lappend branches [list GAP $terminal]}
 }
 if {![llength $branches]} {return [list $root]}
 return [concat [list $root] [join_branches $branches]]
}
proc e2_sll::dfx_bridge::complete_route {plan record ppMapping sourcePP} {
 variable parents;variable prefixes;variable ends;variable sourceAnchor
 variable ppByLeaf;variable bridgeLog;variable nodeCache;variable dynamicLeaves;variable activeChildren
 set parents {};set prefixes {};set ends {};set bridgeLog {};set nodeCache {};set dynamicLeaves {};set activeChildren {}
 set ppByLeaf $ppMapping
 set roots [dict get $record import_selected_outputs]
 if {[llength $roots]!=1} {error "DFX static tree requires one import-selected crossing source"}
 set sourceAnchor [expr {$sourcePP eq {} ? [lindex $roots 0] : $sourcePP}]
 dict set prefixes $sourceAnchor {}
 complete_children [dict get $plan tree] $sourceAnchor {}
 set active [dict create $sourceAnchor 1]
 foreach endpoint [dict keys $ends] {
  set n $endpoint
  while {![dict exists $active $n]} {
   dict set active $n 1
   if {![dict exists $parents $n]} {error "Static tree disconnected at $n"}
   set n [dict get $parents $n]
  }
 }
 dict for {resource segment} [dict get $record segments] {
  if {![dict exists $active $resource]} {error "Planned SLL has no effective terminal: $resource"}
 }
 dict for {child parent} $parents {
  if {[dict exists $active $child]} {dict lappend activeChildren $parent $child}
 }
 set tree [emit $sourceAnchor]
 if {$sourcePP ne {}} {set tree [concat $roots GAP $tree]}
 return $tree
}
