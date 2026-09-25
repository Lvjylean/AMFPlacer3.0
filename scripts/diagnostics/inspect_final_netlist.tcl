# Read-only inventory and transformation evidence for final checkpoint audits.
set_param general.maxThreads 4
if {[catch {
    set root [file normalize [lindex $argv 0]]
    set expected [dict create]
    set f [open "${root}/inputs/expected_cells.tsv" r]
    while {[gets $f line] >= 0} {
        lassign [split $line "\t"] name type
        dict set expected $name $type
    }
    close $f
    open_checkpoint "${root}/reports/faceDetect_amf_routed.dcp"
    set cells [xilinx::designutils::get_leaf_cells *]
    set final [dict create]
    set f [open "${root}/reports/final_cells.tsv" w]
    foreach cell $cells {
        set type [get_property REF_NAME $cell]
        dict set final $cell $type
        puts $f "$cell\t$type\t[get_property LOC $cell]\t[get_property BEL $cell]"
    }
    close $f
    dict for {name type} $expected {
        if {![dict exists $final $name]} {
            puts "MISSING_LEAF\t$name\t$type"
            set existing [get_cells -quiet $name]
            if {[llength $existing]} {report_property -all $existing}
        }
    }
    dict for {name type} $final {
        if {![dict exists $expected $name]} {
            puts "ADDED_LEAF\t$name\t$type"
            report_property -all [get_cells $name]
        }
    }
    close_design
} msg opts]} {
    puts [dict get $opts -errorinfo]
    exit 1
}
exit 0
