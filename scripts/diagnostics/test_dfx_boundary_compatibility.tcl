source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
set tests 0
proc check {name old new should_pass expected_new} {
    global tests
    set failed [catch {dfx_boundary::compare $old $new} result]
    if {$failed == $should_pass} {error "$name: unexpected result: $result"}
    if {!$failed && [dict size [dict get $result newly_assigned]] != $expected_new} {
        error "$name: wrong newly assigned count"
    }
    incr tests
}
set input [dict create p {IN {}} q {OUT INT_X1Y2}]
set placed [dict create p {IN INT_X2Y3} q {OUT INT_X1Y2}]
check identical $input $input 1 0
check fill_empty $input $placed 1 1
check retained_after_place $placed $placed 1 0
check changed_existing $input [dict replace $input q {OUT INT_X9Y9}] 0 0
check cleared_existing $input [dict replace $input q {OUT {}}] 0 0
check changed_direction $input [dict replace $input p {OUT {}}] 0 0
check removed_pin $input [dict remove $input p] 0 0
check added_pin $input [dict merge $input [dict create r {IN {}}]] 0 0
check changed_new_anchor_at_route $placed [dict replace $placed p {IN INT_X9Y9}] 0 0
check malformed $input [dict replace $input p {IN}] 0 0
# Optional real snapshot pairs, read as Tcl data only, never source them.
foreach {before_path after_path expected} $argv {
    set f [open $before_path r]; set old [read $f]; close $f
    set f [open $after_path r]; set new [read $f]; close $f
    check real_snapshots $old $new 1 $expected
}
puts "PASS $tests boundary compatibility cases"
