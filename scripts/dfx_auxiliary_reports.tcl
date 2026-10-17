# Descriptive reports only. No acceptance gate or DCP mutation.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
proc main {} {
    global dfx
    set_param general.maxThreads $dfx(vivado_threads)
    amf_dfx::timed open_routed_for_background_reports {open_checkpoint $dfx(routed_checkpoint)}
    amf_dfx::timed background_utilization {
        report_utilization -slr -file $dfx(output)/utilization.rpt
        report_utilization -hierarchical -file $dfx(output)/hierarchical_utilization.rpt
    }
    amf_dfx::timed background_detailed_paths {
        report_timing -max_paths 100 -path_type full_clock_expanded -file $dfx(output)/critical_paths.rpt
        foreach row $dfx(rps) {
            lassign $row id rp pb
            report_timing -through [get_pins -of_objects [get_cells $rp]] \
                -max_paths 100 -path_type full_clock_expanded -file $dfx(output)/${id}_boundary_paths.rpt
        }
    }
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
