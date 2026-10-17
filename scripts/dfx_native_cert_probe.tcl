# Explicit migration of an already accepted full result. Read-only; not used
# by normal incremental workers or the normal full flow.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_native_certify.tcl]
proc main {} {
    global dfx
    set_param general.maxThreads $dfx(vivado_threads)
    set f [open $dfx(regions_file) r];gets $f;set dfx(rps) {}
    while {[gets $f line]>=0} {lassign [split $line \t] id rp;if {$id ne "static"} {lappend dfx(rps) [list $id $rp]}};close $f
    amf_dfx::timed open_accepted_full {open_checkpoint $dfx(reference_checkpoint)}
    amf_dfx::timed native_cache_certification {amf_native_certify::run $dfx(output)}
    close_design
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
