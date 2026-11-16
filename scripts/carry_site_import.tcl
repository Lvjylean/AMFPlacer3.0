# Import transaction boundaries only. Never change LOC/BEL, shape, or locks.
namespace eval amf_carry_import {
    variable groups {}; variable owners {}; variable targets {}
    variable status {}; variable messages {}; variable report {}
}
proc amf_carry_import::init {data reportPath} {
    variable groups; variable owners; variable targets
    variable status; variable messages; variable report
    set groups {};set owners {};set targets {};set status {};set messages {}
    set f [open $data r]
    try {
        if {[gets $f] ne "site\tcell\ttarget"} {error "Bad CARRY import inventory header"}
        while {[gets $f line]>=0} {
            set fields [split $line "\t"]
            if {[llength $fields]!=3} {error "Malformed CARRY import inventory"}
            lassign $fields site name target
            if {![regexp {^SLICE_X[0-9]+Y[0-9]+$} $site] ||
                [llength [split $target /]]!=2 || [lindex [split $target /] 0] ne $site ||
                $name eq {} || [dict exists $owners $name]} {error "Invalid/duplicate CARRY import member"}
            dict set owners $name $site;dict set targets $name $target
            dict lappend groups $site $name $target
        }
    } finally {close $f}
    set report $reportPath
    set f [open $report w];puts $f "site\tcells\trc\tdetail";close $f
}
proc amf_carry_import::place {pairs} {
    variable groups; variable owners; variable targets
    variable status; variable messages; variable report
    if {[llength $pairs]%2} {error "Odd CARRY import placement list"}
    set normal {};set sites {};set errors {}
    foreach {name target} $pairs {
        if {![dict exists $owners $name]} {lappend normal $name $target;continue}
        if {[dict get $targets $name] ne $target} {error "CARRY import target changed: $name"}
        dict set sites [dict get $owners $name] 1
    }
    # Pure non-CARRY batches retain their original Tcl list bytes and native
    # return/error behavior, including an empty legacy batch.
    if {![dict size $sites]} {return [::place_cell $pairs]}
    foreach site [dict keys $sites] {
        if {![dict exists $status $site]} {
            # A failed site remains failed. Legacy pair retries must not split
            # it, resubmit it, or turn a previous failure into a false success.
            set whole [dict get $groups $site]
            set rc [catch {::place_cell $whole} message]
            dict set status $site $rc;dict set messages $site $message
            if {[catch {
                set f [open $report a]
                try {
                    puts $f [join [list $site [expr {[llength $whole]/2}] $rc [string map [list "\n" " " "\r" " " "\t" " "] $message]] "\t"]
                } finally {close $f}
            } journalError]} {
                dict set status $site 1
                dict set messages $site "Import journal failed after native rc=$rc: $journalError"
                error [dict get $messages $site]
            }
            puts "AMF_CARRY_SITE_IMPORT site=$site cells=[expr {[llength $whole]/2}] rc=$rc"
        }
        if {[dict get $status $site]} {lappend errors "CARRY site $site: [dict get $messages $site]"}
    }
    # Retain the original order and exact targets of unrelated members, even
    # when another site in this legacy batch failed.
    if {[llength $normal] && [catch {::place_cell $normal} message]} {lappend errors $message}
    if {[llength $errors]} {return -code error [join $errors "\n"]}
    return {}
}
