source [file join [file dirname [info script]] dfx_site_import.tcl]
array set expected {};array set seen {};array set siteBatch {};set calls 0
proc place_cell {pairs} {
    global calls expected seen siteBatch
    incr calls
    foreach {cell target} $pairs {
        if {![info exists expected($cell)] || $expected($cell) ne $target || [info exists seen($cell)]} {
            error "Unexpected, modified, or duplicate placement: $cell"
        }
        set seen($cell) 1;set site [lindex [split $target /] 0]
        if {[info exists siteBatch($site)] && $siteBatch($site)!=$calls} {error "Site split across batches: $site"}
        set siteBatch($site) $calls
    }
}
if {[llength $argv]} {
    set f [open [lindex $argv 0]]
    while {[gets $f line]>=0} {lassign [split $line \t] c t;set expected($c) $t};close $f
} else {
    # Names interleave sites; a site may exceed the normal batch size.
    for {set i 0} {$i<19} {incr i} {set expected(cell\[$i\]) "SLICE_X[expr {$i%3}]Y0/BEL$i"}
}
set stats [amf_site_import::place [array get expected] 5]
if {[array size seen]!=[array size expected]} {error "Missing cells"}
puts "PASS: exact cell/LOC/BEL coverage and one batch per site: $stats"
