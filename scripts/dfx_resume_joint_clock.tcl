# Recover a certified joint placed checkpoint; no resynthesis, import or placer.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_joint_checks.tcl]
source [file join [file dirname [info script]] dfx_clock_guidance.tcl]
proc main {} {
    global dfx
    set_param general.maxThreads $dfx(vivado_threads)
    if {$dfx(vivado_threads)!=4 || $dfx(backend_place_mode) ne "route-only" ||
        $dfx(clock_period_ns)!=$dfx(experiment_clock_period_ns) || $dfx(static_routing_containment)} {
        error "Unexpected joint recovery conditions"
    }
    amf_dfx::timed open_joint_imported {open_checkpoint $dfx(recovery_checkpoint)}
    dfx_clock_contract::assert_top recovered 1
    set requests {};set boundaries {}
    foreach row $dfx(rps) {
        lassign $row id rp pb
        dict set requests $id [joint::requested $id $rp]
        dict set boundaries $id [amf_dfx::read_dict $dfx(common_static_metadata)/[set id]_common_static_boundary.tcldata]
    }
    set footprints [amf_dfx::read_dict $dfx(output)/imported_rp_route_footprints.tcldata]
    amf_dfx::timed audit_recovered_imported {joint::audit recovered $requests $boundaries $footprints}
    set f [open $dfx(output)/vivado_placement_not_executed.txt w]
    puts $f {Reused exact joint AMF placed DCP; no placement or explicit unroute executed}
    close $f
    dfx_clock_guidance::apply
    amf_dfx::timed vivado_route {route_design}
    amf_dfx::timed write_joint_routed {write_checkpoint $dfx(output)/joint_routed.dcp}
    puts AMF_JOINT_ROUTE_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
