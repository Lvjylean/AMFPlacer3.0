source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
source [file join [file dirname [info script]] dfx_common_static_shell.tcl]
if {[catch {
    set_param general.maxThreads $dfx(vivado_threads)
    amf_common_static::create $dfx(reference_checkpoint) $dfx(output)
} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
