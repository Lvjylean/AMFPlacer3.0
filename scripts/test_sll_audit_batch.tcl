source [file join [file dirname [info script]] sll_routes.tcl]
proc same {a b} {if {$a ne $b} {error "Expected <$b>, got <$a>"}}
set mode pass
set directionCalls 0
proc get_pips {args} {return {p0 p1 p2}}
proc get_property {property objects} {
 if {$property eq "IS_DIRECTIONAL"} {
  incr ::directionCalls
  if {$::mode eq "metadata"} {return {1 0}}
  return {1 0 1}
 }
 if {$property eq "IS_SLL"} {return [expr {$::mode eq "bidirectional_sll" && $objects eq "C"}]}
 error "Unexpected property $property"
}
proc get_nodes {args} {
 set pip [lindex $args end]
 if {[lindex $args 0] eq "-uphill"} {
  if {$::mode eq "ambiguous" && $pip eq "p2"} {return {C D}}
  return [dict get {p0 A p1 B p2 C} $pip]
 }
 if {[lindex $args 0] eq "-downhill"} {return [dict get {p0 B p1 C p2 D} $pip]}
 return {B C}
}
same [e2_sll::audit_graph net] {A B B C C {B D}}
same $directionCalls 1
foreach mode {metadata ambiguous bidirectional_sll} {
 if {![catch {e2_sll::audit_graph net}]} {error "Expected graph rejection: $mode"}
}
same [e2_sll::trace_paths {A B B {C D} C E D E} A B] {A {} B B C B D B E B}
if {![catch {e2_sll::trace_paths {A {B C} B D C D} A B}]} {error "Conflicting SLL paths accepted"}
# Pin batches preserve literal bracketed names and fail closed on metadata loss.
rename get_property get_property_graph_mock
set pinMode pass
proc get_pins {args} {return {driverObject receiverObject}}
proc get_property {property objects} {
 if {$property eq "NAME"} {
  if {$::pinMode eq "metadata"} {return {drv/O}}
  return [list drv/O {recv_reg[7]/D}]
 }
 if {$property eq "IS_LEAF"} {return {1 1}}
 error "Unexpected pin property $property"
}
set e2_sll::records [list [dict create driver drv/O loads [dict create {recv_reg[7]/D} {}]]]
set e2_sll::cellsByName [dict create drv driverCell {recv_reg[7]} receiverCell]
set e2_sll::pinsByName {};set e2_sll::leafPinsByName {}
same [e2_sll::prepare_audit_pins] 2
same [dict get $e2_sll::leafPinsByName {recv_reg[7]/D}] receiverObject
set pinMode metadata
if {![catch {e2_sll::prepare_audit_pins}]} {error "Incomplete pin batch accepted"}
set pinMode pass;dict unset e2_sll::cellsByName {recv_reg[7]}
if {![catch {e2_sll::prepare_audit_pins}]} {error "Missing endpoint owner accepted"}
puts SLL_AUDIT_BATCH_TESTS_PASSED
