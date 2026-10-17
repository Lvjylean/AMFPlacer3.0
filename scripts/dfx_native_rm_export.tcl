source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_native_export.tcl]
proc main {} {
    global dfx
    set_param general.maxThreads $dfx(vivado_threads)
    amf_dfx::timed open_optimized_ooc {open_checkpoint $dfx(rm_checkpoint)}
    if {[llength [get_cells -hier -filter {IS_BLACKBOX}]]} {error "Unresolved OOC blackbox"}
    set c [get_clocks $dfx(clock_name)]
    if {[llength $c]!=1 || abs([get_property PERIOD $c]-$dfx(clock_period_ns))>1e-6} {error "OOC clock mismatch"}
    amf_dfx::timed export_ooc_module {amf_native_export::module $dfx(output) $dfx(module_id)}
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
