# Structural checks on Vivado directed routing strings. A GAP is not proof of
# physical connectivity; final validation must still trace the used PIP graph.
namespace eval e2_sll {}
proc e2_sll::route_structure {tree resources {prefix {}} {stub 0}} {
 set result {}
 foreach item $tree {
  if {[llength $item]>1} {
   set child [route_structure $item $resources $prefix $stub]
   dict for {node witnesses} $child {
    foreach witness $witnesses {dict lappend result $node $witness}
   }
   continue
  }
  if {$item eq "STUB"} {set stub 1;continue}
  if {$item eq "GAP" || $item eq ""} {continue}
  # Vivado can qualify the entering wire of a multi-tile node, e.g. <1>.
  regsub {^<[0-9]+>} $item {} node
  if {[dict exists $resources $node]} {lappend prefix $node}
  dict lappend result $node [dict create sll $prefix stub $stub]
 }
 return $result
}
proc e2_sll::structure_errors {tree resources receivers} {
 set nodes [route_structure $tree $resources]
 set errors {}
 dict for {node unused} $resources {
  if {![dict exists $nodes $node]} {lappend errors [list missing_sll $node];continue}
  foreach witness [dict get $nodes $node] {
   if {[dict get $witness stub]} {lappend errors [list stub_sll $node]}
  }
 }
 dict for {node expected} $receivers {
  if {![dict exists $nodes $node]} {lappend errors [list missing_receiver $node];continue}
  foreach witness [dict get $nodes $node] {
   if {[dict get $witness stub]} {
    lappend errors [list stub_receiver $node]
   } elseif {[dict get $witness sll] ne $expected} {
    lappend errors [list receiver_group_changed $node $expected [dict get $witness sll]]
   }
  }
 }
 return $errors
}
