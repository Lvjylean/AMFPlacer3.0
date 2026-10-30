source [file join [file dirname [info script]] dfx_full_carry_preroute.tcl]
namespace eval e2_sll {}
set ::routeCalls 0
set ::mode pass
proc get_nets {args} {if {$::mode eq "missing"} {return {}};return net}
proc get_pins {args} {
    if {[lsearch -exact $args -filter]>=0} {return driver}
    if {$::mode eq "connectivity" && $::routeCalls} {return {driver other_sink}}
    return {driver sink}
}
proc get_cells {args} {return cell}
proc get_nodes {args} {return sll_node}
proc get_property {property objects} {
    switch -- $property {
        REF_NAME {if {$::mode eq "driver"} {return LUT6};return CARRY8}
        NAME {return $objects}
        LOC {if {$::mode eq "placement" && $::routeCalls} {return changed};return original}
        BEL {return CARRY8}
        LOCK_PINS {return {}}
        FIXED_ROUTE {return {HMUX GAP sll_node}}
        ROUTE_STATUS {if {$::mode eq "partial"} {return PARTIAL};return ROUTED}
    }
    error "Unexpected property $property"
}
proc route_design {args} {incr ::routeCalls}
proc reset_record {} {
    set ::e2_sll::records [list [dict create net target source_mode vivado_native_router_source segments [dict create sll_node {a b sll_node c d}]]]
    set ::routeCalls 0
}
set out [file join [file dirname [info script]] unit-output]
file mkdir $out
foreach mode {missing driver connectivity placement partial} {
    reset_record
    if {![catch {full_carry_preroute::initialize target $out} message]} {error "Expected rejection: $mode"}
    if {$mode in {missing driver} && $::routeCalls} {error "Mutated before validation: $mode"}
}
set mode pass
reset_record
if {![catch {full_carry_preroute::initialize absent $out}]} {error "Missing audit target accepted"}
if {$::routeCalls} {error "Routed an unplanned target"}
reset_record
full_carry_preroute::initialize target $out
if {$::routeCalls!=1} {error "Expected one net-only route"}
puts CARRY_PREROUTE_GUARD_TESTS_PASSED
