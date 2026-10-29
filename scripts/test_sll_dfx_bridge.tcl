source [file join [file dirname [info script]] sll_dfx_bridge.tcl]
source [file join [file dirname [info script]] sll_route_structure.tcl]
proc equal {actual expected} {if {$actual ne $expected} {error "Expected <$expected>, got <$actual>"}}
# Reconvergence within a group reuses its first parent; cross-group merging
# must fail rather than silently connect a receiver through another SLL.
set ::e2_sll::dfx_bridge::parents {}
set ::e2_sll::dfx_bridge::prefixes {R {}}
e2_sll::dfx_bridge::insert {R A B} {}
e2_sll::dfx_bridge::insert {R C B D} {}
equal [dict get $::e2_sll::dfx_bridge::parents B] A
equal [dict get $::e2_sll::dfx_bridge::parents D] B
e2_sll::dfx_bridge::insert {R E S X} {} S
equal [dict get $::e2_sll::dfx_bridge::prefixes X] S
if {![catch {e2_sll::dfx_bridge::insert {X B} S}]} {error "Crossing group merge accepted"}
# Exercise option construction with an empty and a nonempty exclusion set.
rename e2_sll::dfx_bridge::node e2_sll::dfx_bridge::real_node
proc e2_sll::dfx_bridge::node {name} {return $name}
proc get_property {property node} {return 0}
proc find_routing_path {args} {
 set options [dict create {*}$args]
 if {[dict exists $options -exclude_nodes] && ![llength [dict get $options -exclude_nodes]]} {error "Empty exclusion option"}
 return [list [dict get $options -from] [dict get $options -to]]
}
set ::e2_sll::dfx_bridge::parents {}
set ::e2_sll::dfx_bridge::prefixes {R {}}
set ::e2_sll::dfx_bridge::bridgeLog {}
e2_sll::dfx_bridge::complete_connection R A {}
equal [dict get $::e2_sll::dfx_bridge::parents A] R
dict set ::e2_sll::dfx_bridge::prefixes FOREIGN DIFFERENT
e2_sll::dfx_bridge::complete_connection A B {}
equal [dict get $::e2_sll::dfx_bridge::parents B] A
# A free site input is not a legal transit resource for an unrelated signal.
# Retry with that node excluded; exhausted searches must not emit a tree.
rename get_property {};rename find_routing_path {}
proc get_property {property node} {expr {$property eq "IS_PIN" && $node eq "OTHER_SIGNAL_PIN"}}
set searches 0
proc find_routing_path {args} {
 incr ::searches
 set options [dict create {*}$args]
 if {![dict exists $options -exclude_nodes] || "OTHER_SIGNAL_PIN" ni [dict get $options -exclude_nodes]} {
  return [list [dict get $options -from] OTHER_SIGNAL_PIN [dict get $options -to]]
 }
 return [list [dict get $options -from] SAFE_WIRE [dict get $options -to]]
}
set ::e2_sll::dfx_bridge::parents {}
set ::e2_sll::dfx_bridge::prefixes {R {}}
e2_sll::dfx_bridge::complete_connection R T {}
equal $searches 2
equal [dict get $::e2_sll::dfx_bridge::parents T] SAFE_WIRE
if {[dict exists $::e2_sll::dfx_bridge::parents OTHER_SIGNAL_PIN]} {error "Transit pin was accepted"}
rename find_routing_path {}
proc find_routing_path {args} {incr ::searches;return {R OTHER_SIGNAL_PIN T}}
set searches 0
set ::e2_sll::dfx_bridge::parents {}
set ::e2_sll::dfx_bridge::prefixes {R {}}
if {![catch {e2_sll::dfx_bridge::complete_connection R T {}} message] || ![string match {*retry budget*} $message]} {error "Unsafe search exhaustion accepted"}
equal $searches 16
equal $::e2_sll::dfx_bridge::parents {}
rename get_property {};rename find_routing_path {}
rename e2_sll::dfx_bridge::node {}
rename e2_sll::dfx_bridge::real_node e2_sll::dfx_bridge::node
# Mock only device path search; exercise real complete-tree construction,
# shared PP grouping, pruning, serialization and structural path validation.
rename e2_sll::dfx_bridge::complete_connection e2_sll::dfx_bridge::device_connection
proc e2_sll::dfx_bridge::complete_connection {from to prefix} {
 variable prefixes
 if {![dict exists $prefixes $to]} {insert [list $from $to] $prefix}
}
proc e2_sll::terminal {name slr} {return [list $name]}
set plan {tree {{pin LOCAL slr 0} {resource S1 segment {E1 T1 S1 R1 X1} children {{pin A slr 1} {pin B slr 1} {pin C slr 1} {resource S2 segment {E2 T2 S2 R2 X2} children {{pin D slr 2}}}}}}}
set record {import_selected_outputs ROOT segments {S1 {E1 T1 S1 R1 X1} S2 {E2 T2 S2 R2 X2}}}
set route [e2_sll::dfx_bridge::complete_route $plan $record {A PP B PP} {}]
equal [e2_sll::structure_errors $route [dict get $record segments] {A S1 B S1 C S1 D {S1 S2}}] {}
set structure [e2_sll::route_structure $route [dict get $record segments]]
equal [llength [dict get $structure PP]] 1
if {[dict exists $structure LOCAL]} {error "Local unconstrained receiver unexpectedly fixed"}
set route [e2_sll::dfx_bridge::complete_route $plan $record {A PP B PP} SOURCE_PP]
equal [lrange $route 0 2] {ROOT GAP SOURCE_PP}
equal [e2_sll::structure_errors $route [dict get $record segments] {A S1 B S1 D {S1 S2}}] {}
# Missing effective terminal cannot be hidden by keeping an orphan SLL.
dict set record segments ORPHAN {E T ORPHAN R X}
if {![catch {e2_sll::dfx_bridge::complete_route $plan $record {A PP B PP} {}}]} {error "Orphan SLL accepted"}
puts SLL_DFX_BRIDGE_TESTS_PASSED
