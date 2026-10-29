source [file join [file dirname [info script]] .. scripts sll_routes.tcl]
proc get_cells {args} {return carry}
proc get_property {key objects} {
 if {![llength $objects]} {error "empty property query"}
 if {$key eq "REF_NAME"} {return $::ref}
 if {$key eq "NAME"} {return $objects}
 error "unexpected property $key"
}
proc get_nodes {args} {
 if {[lsearch -exact $args -downhill]>=0} {return $::downstream}
 return [lindex $args end]
}
set root TILE/CLE_COUT
set fabric TILE/CLE_HMUX
set cases {
 {dead_end CARRY8 {} 1}
 {carry_only CARRY8 {NEXT/CLE_CIN_PIN} 1}
 {fabric_reachable CARRY8 {NEXT/CLE_CIN_PIN INT/CHAN} 0}
 {not_carry LUT4 {} 0}
}
foreach case $cases {
 lassign $case name ref downstream expected
 set actual [e2_sll::needs_native_source_resolution driver/CO7 [list $root] [list $root $fabric]]
 if {$actual!=$expected} {error "$name expected $expected got $actual"}
 puts "PASS $name"
}
set ref CARRY8;set downstream {}
foreach selected [list {} [list $fabric] [list $root $fabric]] {
 if {[e2_sll::needs_native_source_resolution driver/CO7 $selected [list $root $fabric]]} {error "non-COUT selection incorrectly requests fallback"}
}
if {[e2_sll::needs_native_source_resolution driver/CO7 [list $root] [list $root]]} {error "unique output has no alternative"}
puts "PASS selection_guards"

# A property reset alone must not leave the old COUT as the router's root.
rename get_property get_property_source_test
proc get_property {key object} {
 if {$key eq "NAME"} {return $object}
 if {$key eq "ROUTE"} {lappend ::events read_route;return [list $::fabric INT/WIRE]}
 return [get_property_source_test $key $object]
}
proc get_nets {name} {return $name}
proc reset_property {key net} {lappend ::events reset}
proc route_design {args} {
 if {[lsearch -exact $args -unroute]>=0} {lappend ::events unroute;return}
 lappend ::events route
 if {$::fail_route} {error "injected route failure"}
}
set events {};set fail_route 0
set result [e2_sll::native_source_route target_net [list $root GAP SLL/WIRE] [list $root $fabric]]
if {$events ne {reset unroute route read_route unroute}} {error "wrong cleanup sequence: $events"}
if {$result ne [list $fabric GAP SLL/WIRE]} {error "probe must retain only chosen source and original SLL tail: $result"}
set events {};set fail_route 1
if {![catch {e2_sll::native_source_route target_net [list $root GAP SLL/WIRE] [list $root $fabric]} failure]} {error "route failure was swallowed"}
if {$failure ne "injected route failure" || $events ne {reset unroute route unroute}} {error "failure cleanup mismatch: $failure / $events"}
puts "PASS native_source_cleanup_and_failure"
