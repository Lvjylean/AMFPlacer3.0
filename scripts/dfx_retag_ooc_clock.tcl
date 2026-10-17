# Recover an existing, exactly AMF-placed OOC module without synthesis or placement.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_ooc_clock_handoff.tcl]
proc locations {} {
    set result {}
    foreach c [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}] {
        dict set result [get_property NAME $c] [list [get_property REF_NAME $c] [get_property LOC $c] [get_property BEL $c] [get_property IS_LOC_FIXED $c] [get_property IS_BEL_FIXED $c]]
    }
    return $result
}
proc verify_requested {} {
    global dfx
    set f [open $dfx(placement_table) r];set count 0;set seen {}
    set prefix "$dfx(active_partition)/"
    while {[gets $f line]>=0} {
        if {$line eq ""} {continue}
        lassign [split $line \t] full target
        if {[string first $prefix $full]!=0 || [dict exists $seen $full]} {error "Invalid placement key"}
        dict set seen $full 1
        set local [string range $full [string length $prefix] end]
        set c [get_cells -quiet [list $local]]
        if {[llength $c]!=1} {error "Missing placed cell: $full"}
        set actual [get_property LOC $c]
        if {[string first / $target]>=0} {append actual /[lindex [split [get_property BEL $c] .] end]}
        if {$actual ne $target} {error "AMF placement changed: $full"}
        incr count
    }
    close $f
    if {$count!=$dfx(expected_cells)} {error "Incomplete AMF placement inventory"}
    return $count
}
proc main {} {
    global dfx
    set_param general.maxThreads 4
    amf_dfx::timed open_existing_module {open_checkpoint $dfx(source_module)}
    set before [locations]
    set count [verify_requested]
    set clocks [get_clocks -quiet *]
    set clk [get_clocks -quiet $dfx(clock_name)]
    if {[llength $clocks]!=1 || [llength $clk]!=1 || [get_property IS_GENERATED $clk]} {error "Unexpected OOC clock inventory"}
    if {abs([get_property PERIOD $clk]-$dfx(clock_period_ns))>1e-6} {error "OOC period changed"}
    if {[get_property SOURCE_PINS $clk] ne $dfx(rm_clock_port)} {error "Unexpected OOC primary source"}
    amf_dfx::timed strip_handoff_primary {amf_ooc_clock::strip_checked_primary}
    amf_dfx::assert_same $before [locations] "OOC clock reclassification changed placement"
    verify_requested
    amf_dfx::timed write_retagged_module {write_checkpoint $dfx(output)/rm_amf_placed.dcp}
    close_design
    amf_dfx::timed reopen_retagged_module {open_checkpoint $dfx(output)/rm_amf_placed.dcp}
    amf_dfx::assert_same $before [locations] "OOC checkpoint roundtrip changed placement"
    verify_requested
    if {[llength [get_clocks -quiet *]]!=0} {error "OOC primary reappeared after handoff roundtrip"}
    set f [open $dfx(output)/retention.tsv w];puts $f "cells\t$count\nloc_bel_exact\t1\nfixed_flags_exact\t1\nplace_design_executed\t0";close $f
    puts AMF_OOC_CLOCK_RETAG_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
