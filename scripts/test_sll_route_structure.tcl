source [file join [file dirname [info script]] sll_route_structure.tcl]
proc equal {actual expected} {if {$actual ne $expected} {error "Expected <$expected>, got <$actual>"}}
set resources {L/UBUMP0 {} M/UBUMP1 {}}
set tree {SRC {GAP L/UBUMP0 {GAP A/IN} GAP M/UBUMP1 GAP B/IN} GAP LOCAL/IN}
equal [e2_sll::structure_errors $tree $resources {A/IN L/UBUMP0 B/IN {L/UBUMP0 M/UBUMP1} LOCAL/IN {}}] {}
set tree {{SRC {GAP STUB L/UBUMP0 {GAP A/IN}} GAP B/IN}}
equal [e2_sll::structure_errors $tree {L/UBUMP0 {}} {A/IN L/UBUMP0 B/IN {}}] {{stub_sll L/UBUMP0} {stub_receiver A/IN}}
equal [e2_sll::structure_errors {{SRC {GAP L/UBUMP0} GAP A/IN}} {L/UBUMP0 {}} {A/IN L/UBUMP0}] {{receiver_group_changed A/IN L/UBUMP0 {}}}
equal [e2_sll::structure_errors {SRC GAP <1>L/UBUMP0 GAP A/IN} {L/UBUMP0 {}} {A/IN L/UBUMP0}] {}
equal [e2_sll::structure_errors {SRC GAP L/UBUMP0 GAP A/IN} {L/UBUMP0 {} M/UBUMP1 {}} {B/IN M/UBUMP1}] {{missing_sll M/UBUMP1} {missing_receiver B/IN}}
equal [e2_sll::structure_errors {SRC {GAP L/UBUMP0 GAP A/IN} GAP A/IN} {L/UBUMP0 {}} {A/IN L/UBUMP0}] {{receiver_group_changed A/IN L/UBUMP0 {}}}
equal [e2_sll::structure_errors {SRC GAP MY_STUB_NAMED_NODE GAP A/IN} {} {A/IN {}}] {}
puts SLL_ROUTE_STRUCTURE_TESTS_PASSED
