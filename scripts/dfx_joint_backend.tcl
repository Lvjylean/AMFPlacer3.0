# Both-empty-RP static shell + both AMF placed modules + one joint route. No placer.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
source [file join [file dirname [info script]] dfx_common_static_shell.tcl]
source [file join [file dirname [info script]] dfx_joint_checks.tcl]
proc main {} {
 global dfx
 set out $dfx(output)
 set_param general.maxThreads $dfx(vivado_threads)
 if {$dfx(vivado_threads)!=4 || $dfx(clock_period_ns)!=$dfx(experiment_clock_period_ns) || $dfx(static_routing_containment)} {error "Unexpected joint backend conditions"}
 if {![info exists dfx(backend_place_mode)] || $dfx(backend_place_mode) ne "route-only"} {error "Explicit route-only backend contract required"}
 set requests {}
 # Module inputs already carry the optimized netlist and exact AMF coordinates.
 # They are created at AMF import completion, before any Vivado placer/router.
 foreach row $dfx(rps) {
  lassign $row id rp pb
  dict set requests $id [joint::requested $id $rp]
 }
 # The producer already removed every old RM and locked static routing.
 # Reject an implemented-RM input instead of silently blackboxing it here.
 amf_dfx::timed open_common_static_shell {open_checkpoint $dfx(common_static_checkpoint)}
 set boundaries {}
 foreach row $dfx(rps) {
  lassign $row id rp pb
  dict set boundaries $id [amf_dfx::read_dict [file join $dfx(common_static_metadata) [set id]_common_static_boundary.tcldata]]
 }
 amf_dfx::timed verify_common_static_input {amf_common_static::assert_empty consumer $boundaries}
 dfx_clock_contract::assert_top common_static
 set original_clocks [dfx_clock_contract::signature]
 if {[amf_dfx::static_audit_enabled]} {amf_dfx::save_dict $out/static_snapshot.tcldata [amf_multi::static_snapshot]}
 report_route_status -file $out/locked_static_route_status.rpt
 foreach row $dfx(rps) {
  lassign $row id rp pb
  amf_dfx::timed import_amf_placed_$id {read_checkpoint -strict -cell $rp [set dfx(rm_[set id]_checkpoint)]}
  dfx_clock_contract::assert_unchanged $original_clocks after_import_$id
 }
 foreach f $dfx(assembly_xdc_files) {read_xdc $f}
 set footprints {}
 foreach row $dfx(rps) {lassign $row id rp pb;dict set footprints $id [amf_multi::route_footprint $rp]}
 amf_dfx::save_dict $out/imported_rp_route_footprints.tcldata $footprints
 amf_dfx::timed audit_imported {joint::audit imported $requests $boundaries $footprints}
 amf_dfx::timed write_joint_imported {write_checkpoint $out/joint_amf_placed.dcp}
 # The exact AMF placement has just passed complete coverage/LOC/BEL/RP checks.
 # No placement fallback is permitted. A router prerequisite failure remains a failure.
 set f [open $out/vivado_placement_not_executed.txt w]
 puts $f {User route-only contract: AMF provides complete placement; no Vivado placement executed}
 close $f
 # The only route_design in this workflow routes both new RMs together.
 amf_dfx::timed vivado_route {route_design}
 amf_dfx::timed write_joint_routed {write_checkpoint $out/joint_routed.dcp}
 puts AMF_JOINT_ROUTE_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
