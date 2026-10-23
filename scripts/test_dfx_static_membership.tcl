source [file join [file dirname [info script]] dfx_static_membership.tcl]
set tests 0
proc check {names direct hierarchy rp missing unexpected} {
    set d [dict create]; foreach n $direct {dict set d $n 1}
    set h [dict create]; foreach n $hierarchy {dict set h $n 1}
    set r [amf_static_membership::classify $names $d $h $rp]
    if {[dict get $r missing] ne $missing || [dict get $r unexpected] ne $unexpected} {error "Unexpected classification: $r"}
    incr ::tests
}
check {top/a top/b} {top/a top/b} {} {} {} {}
check {top/a top/b} {top/a} {} {} top/b {}
check {top/a/x top/a/y top/a2/z} top/a top/a {} top/a2/z {}
check {top/a/x/y top/a/u/v} top/a top/a {} {} {}
check {top/rp/x top/static/y} top/static top/static top/rp {} {}
check {top/rp/x top/static/y} top top top/rp {} top/rp/x
check {top/rp/x top/rp2/y} {} {} top/rp top/rp2/y {}
check {{top/a[0]/x} {top/a[1]/x}} {{top/a[0]}} {{top/a[0]}} {} {{top/a[1]/x}} {}
# Primitive membership cannot confer membership to a textual descendant.
check {top/a top/a/x} top/a {} {} top/a/x {}
check {top/rp} top top top/rp {} top/rp
check {a b c} {a c} {} {} b {}
check {} {} {} {} {} {}
puts "PASS $tests membership closure cases"
