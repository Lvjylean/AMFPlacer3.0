source [file join [file dirname [info script]] .. sll_routes.tcl]
proc get_nets {name} {global queriedNet;set queriedNet $name;return resolved_net_object}
proc set_property {property value object} {global replayArgs;set replayArgs [list $property $value $object]}
set specialName {top/macro/dout[19]_INST_0_i_19_n_3}
set sampleRoute {GAP node_a {GAP node_b sink_b} GAP sink_a}
eval [e2_sll::constraint_command $sampleRoute $specialName]
if {$queriedNet ne $specialName || $replayArgs ne [list FIXED_ROUTE $sampleRoute resolved_net_object]} {error "Exported command lost net identity or routing tree"}
rename get_nets {};rename set_property {}
set selected [e2_sll::source_accesses {LOGICAL_DRIVER {mux GAP sll_a sink1} direct GAP sink2} {direct mux unused}]
if {$selected ne {direct mux}} {error "Nested normalized source exits not identified: $selected"}
rename e2_sll::validate_endpoint e2_sll::native_validate_endpoint
proc e2_sll::validate_endpoint {name slr} {}
rename e2_sll::terminal e2_sll::native_terminal
proc e2_sll::terminal {name slr} {return shared_input}
set e2_sll::receiverNodePaths {};set loads {};set segments {}
set tree [e2_sll::children [list [dict create pin ff0/CE slr 2] [dict create pin ff1/CE slr 2]] {sll_a} loads segments]
if {$tree ne {GAP shared_input} || [dict size $loads]!=2} {error "Shared physical input was duplicated or logical receiver was lost"}
set loads2 {};set segments2 {}
if {![catch {e2_sll::children [list [dict create pin ff2/CE slr 2]] {sll_b} loads2 segments2} why] || ![string match {*conflicting SLL groups*} $why]} {error "Conflicting physical receiver groups accepted"}
rename e2_sll::terminal {}
rename e2_sll::native_terminal e2_sll::terminal
rename e2_sll::validate_endpoint {}
rename e2_sll::native_validate_endpoint e2_sll::validate_endpoint
set graph [dict create direct {local} mux {sll_a} sll_a {remote1 sll_b} sll_b {remote2}]
set paths [e2_sll::trace_paths $graph {direct mux} {sll_a sll_b}]
if {[dict get $paths local] ne {} || [dict get $paths remote1] ne {sll_a} || [dict get $paths remote2] ne {sll_a sll_b}} {
 error "Wrong local/one-crossing/two-crossing paths for a multi-output net"
}
if {[dict exists $paths disconnected]} {error "Disconnected receiver accepted"}
dict lappend graph direct remote1
if {![catch {e2_sll::trace_paths $graph {direct mux} {sll_a sll_b}} why]} {error "SLL bypass hidden by first-hit BFS"}
set graph [dict create out1 {shared} out2 {shared} shared {sll_a} sll_a {sink}]
if {[dict get [e2_sll::trace_paths $graph {out1 out2} {sll_a}] sink] ne {sll_a}} {
 error "Equivalent physical roots incorrectly rejected"
}
puts "E2_SLL_ROUTE_GRAPH_TEST_PASS"
