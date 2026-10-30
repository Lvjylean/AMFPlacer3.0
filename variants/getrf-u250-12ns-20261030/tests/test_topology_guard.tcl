source [lindex $argv 0]
set out [lindex $argv 1]
file mkdir $out
set expected {n[1]}
set changes {}
set touched [dict create]
set sinks {P_IN_1 P_IN_2}
proc get_nets {args} {
    if {[lsearch -exact $args -segments]>=0} {return {N0 N_ALIAS}}
    set f [lindex $args end]
    if {$f eq {NAME == "n[1]"}} {return N0}
    return {}
}
proc get_pins {args} {
    if {[lindex $args end] eq {DIRECTION == OUT}} {return P_OUT}
    return $::sinks
}
proc get_cells {args} {return C0}
proc get_property {property objects} {
    set result {}
    foreach object $objects {
        switch -- $property {
            NAME {lappend result $object}
            IS_PRIMITIVE {lappend result 1}
            DONT_TOUCH {lappend result [expr {[dict exists $::touched $object]?[dict get $::touched $object]:0}]}
            default {error "Unexpected mock property"}
        }
    }
    return $result
}
proc set_property {key value objects} {
    lappend ::changes [list $key $value $objects]
    foreach object $objects {dict set ::touched $object $value}
}
set names $out/names.txt
set f [open $names w];puts $f {n[1]};puts $f missing;close $f
if {![catch {full_topology_guard::apply $names $out}]} {error "Missing exact net was accepted"}
if {[llength $changes]} {error "Partial request changed properties before validation"}
set f [open $names w];puts $f {n[1]};close $f
full_topology_guard::apply $names $out
if {[lsort [dict keys $touched]] ne {C0 N0 N_ALIAS}} {error "Wrong protection scope"}
full_topology_guard::verify $out
set sinks {P_IN_1}
if {![catch {full_topology_guard::verify $out}]} {error "Receiver change was accepted"}
puts TOPOLOGY_GUARD_TESTS_PASSED
