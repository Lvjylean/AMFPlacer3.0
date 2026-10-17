# Historical migration validation only. Future full runs emit these interfaces
# during the normal full export, without opening a second full checkpoint.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_native_export.tcl]
proc main {} {
    global dfx
    set_param general.maxThreads $dfx(vivado_threads)
    set f [open $dfx(regions_file) r];gets $f
    set dfx(rps) {}
    while {[gets $f line]>=0} {
        lassign [split $line \t] id instance
        if {$id ne "static"} {lappend dfx(rps) [list $id $instance]}
    };close $f
    amf_dfx::timed open_full_reference {open_checkpoint $dfx(rm_checkpoint)}
    amf_dfx::timed full_interface_export {amf_native_export::full_interfaces $dfx(output)}
    amf_dfx::timed partition_nodes_export {amf_native_export::partition_nodes $dfx(output)}
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
