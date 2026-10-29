# Query only SLL-segment candidate nodes; never expand all routed design nets.
namespace eval e2_sll_inventory {}
proc e2_sll_inventory::used_candidates {candidates {batch_size 4096}} {
 if {$batch_size<1} {error "Invalid SLL inventory batch size"}
 set used {};set requested [dict keys $candidates];set aliases 0
 for {set offset 0} {$offset<[llength $requested]} {incr offset $batch_size} {
  set batch [lrange $requested $offset [expr {$offset+$batch_size-1}]]
  set objects [get_nodes -quiet $batch]
  if {![llength $objects]} {error "SLL inventory batch resolved no device nodes"}
  set canonical {}
  foreach name [get_property NAME $objects] occupied [get_property IS_USED $objects] {
   dict set canonical $name $occupied
  }
  foreach name $batch {
   if {[dict exists $canonical $name]} {
    set occupied [dict get $canonical $name]
   } else {
    # A named wire may resolve to a differently named canonical node.
    set object [get_nodes -quiet [list $name]]
    if {[llength $object]!=1} {error "Missing/ambiguous SLL inventory node: $name"}
    set occupied [get_property IS_USED $object]
    incr aliases
   }
   if {$occupied} {dict set used $name 1}
  }
 }
 puts "E2_SLL_CANDIDATE_INVENTORY candidates=[llength $requested] used=[dict size $used] aliases=$aliases batch=$batch_size"
 return $used
}
# A physical resource includes both directed views. All of its segment tiles
# must fit within one RP footprint; joining pieces from different RPs is invalid.
proc e2_sll_inventory::eligible_candidates {resource_nodes footprints} {
 set candidates {};set eligible 0
 dict for {resource nodes} $resource_nodes {
  set possible [dict keys $footprints]
  foreach node $nodes {
   set tile [lindex [split $node /] 0];set next {}
   foreach rp $possible {if {[dict exists $footprints $rp $tile]} {lappend next $rp}}
   set possible $next
   if {![llength $possible]} {break}
  }
  if {[llength $possible]} {
   incr eligible
   foreach node $nodes {dict set candidates $node 1}
  }
 }
 puts "E2_SLL_FOOTPRINT_FILTER physical_resources=[dict size $resource_nodes] eligible_resources=$eligible candidate_nodes=[dict size $candidates]"
 return $candidates
}
