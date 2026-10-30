source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_static_containment.tcl]
source [file join [file dirname [info script]] dfx_static_membership.tcl]
source [file join [file dirname [info script]] dfx_full_input.tcl]
source [file join [file dirname [info script]] dfx_site_import.tcl]
source [file join [file dirname [info script]] dfx_full_audit.tcl]
source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
source [file join [file dirname [info script]] dfx_common_static_shell.tcl]
source [file join [file dirname [info script]] dfx_sll.tcl]
proc main {} {
    global dfx
    set out $dfx(output);set_param general.maxThreads $dfx(vivado_threads)
    set resumeStage opt
    if {[info exists dfx(resume_full_stage)]} {set resumeStage $dfx(resume_full_stage)}
    if {$resumeStage ni {opt imported placed routed}} {error "Invalid full checkpoint stage: $resumeStage"}
    amf_dfx::timed open_full_$resumeStage {open_checkpoint $out/base_${resumeStage}.dcp}
    amf_multi::validate_regions
    amf_static::recover_validate
    array set requested {};set f [open $out/placement.tsv r]
    while {[gets $f line]>=0} {
        lassign [split $line \t] c target
        if {[info exists requested($c)]} {error "Duplicate full AMF placement: $c"}
        set requested($c) $target
    };close $f
    if {![array size requested]} {error "Missing full AMF placement"}
    if {$resumeStage eq "opt"} {
        amf_dfx::timed reset_full_lut_groups {
            foreach prop {HLUTNM SOFT_HLUTNM} {
                set grouped [get_cells -quiet -hierarchical -filter "$prop != \"\""]
                if {[llength $grouped]} {set_property $prop {} $grouped}
            }
        }
        amf_dfx::timed import_full_amf {amf_site_import::place [array get requested]}
        amf_dfx::timed audit_imported {full_region_audit [array get requested] imported}
        amf_dfx::timed write_imported {write_checkpoint $out/base_imported.dcp}
    } elseif {$resumeStage eq "imported"} {
        amf_dfx::timed audit_reused_imported {full_region_audit [array get requested] imported}
    }
    # Save completed work before a diagnostic can fail. A saved DCP is not
    # acceptance: all region, routing, DRC and interface checks remain mandatory.
    if {$resumeStage in {opt imported}} {
        amf_dfx::timed static_membership_pre_place {amf_static_membership::enforce pre_place 1}
        source [file join [file dirname [info script]] dfx_full_topology_guard.tcl]
        amf_dfx::timed protect_confirmed_topology {full_topology_guard::apply [file join [file dirname [info script]] topology-guard-nets.txt] $out}
        amf_dfx::timed vivado_place {place_design}
        amf_dfx::timed verify_confirmed_topology {full_topology_guard::verify $out}
        amf_dfx::timed write_placed_dcp {write_checkpoint $out/base_placed.dcp}
    }
    if {$resumeStage ne "routed"} {
        amf_dfx::timed static_membership_pre_route {amf_static_membership::enforce pre_route 1}
        amf_dfx::timed write_membership_placed_dcp {write_checkpoint $out/base_membership_placed.dcp}
        amf_dfx::timed audit_placed {full_region_audit [array get requested] placed}
        amf_dfx::timed placed_timing_report {report_timing_summary -delay_type min_max -file $out/base_placed_timing.rpt}
        amf_dfx::timed full_sll_import {dfx_sll::apply_full}
        if {[info exists dfx(e2_sll_enabled)] && $dfx(e2_sll_enabled)} {
            source [file join [file dirname [info script]] dfx_full_carry_preroute.tcl]
            amf_dfx::timed full_carry_source_init {full_carry_preroute::initialize {kernel/grp_getrf_core_double_2_512_256_s_fu_1411/grp_subUpdate_double_2_512_s_fu_7138/grp_subUpdate_double_2_512_Pipeline_LoopMulSub_fu_44/dsub_64ns_64ns_64_5_full_dsp_1_U1563/kernel_getrf_0_dsub_64ns_64ns_64_5_full_dsp_1_ip_u/U0/i_synth/ADDSUB_OP.ADDSUB/LOGIC_SPEED.OP/NORM/LZE/ZERO_DET_CC_1/CARRYS_OUT[7]} $out}
        }
        amf_dfx::timed vivado_route {route_design}
        amf_dfx::timed write_routed_dcp {write_checkpoint $out/base_routed.dcp}
    }
    if {$resumeStage eq "routed"} {dfx_sll::restore_full_audit}
    set policy strict
    if {[info exists dfx(e2_sll_audit_policy)]} {set policy $dfx(e2_sll_audit_policy)}
    if {$policy ni {strict record-failure}} {error "Unknown full SLL audit policy: $policy"}
    set rc [catch {amf_dfx::timed full_sll_audit {dfx_sll::audit $out/base_routed.dcp}} msg opts]
    set checked [expr {[info exists dfx(e2_sll_enabled)] && $dfx(e2_sll_enabled)}]
    set f [open $out/sll-audit-status.tsv w]
    puts $f "checked\t$checked";puts $f "passed\t[expr {!$rc}]";puts $f "policy\t$policy"
    if {$checked} {dict for {key value} [dfx_sll::full_audit_scope] {puts $f "$key\t$value"}}
    if {$rc} {puts $f "error\t[string map [list \t { } \n { } \r { }] $msg]"}
    close $f
    if {$rc} {
        if {$policy eq "strict"} {return -options $opts $msg}
        puts "EXPERIMENTAL_BASELINE_SLL_AUDIT_FAILED: $msg"
        # Keep the failed stage exit code and measure real occupancy separately.
        amf_dfx::timed full_sll_usage {e2_sll::usage [file join $out sll]}
    }
    amf_dfx::timed static_membership_routed {amf_static_membership::enforce routed 0}
    amf_dfx::timed audit_routed {full_region_audit [array get requested] routed}
    amf_dfx::timed reports {amf_multi::reports $out/base}
    amf_dfx::timed static_route_footprint_audit {amf_static::routed_audit}
    foreach row $dfx(rps) {
        lassign $row id rp pb;set dfx(active_partition) $rp
        amf_dfx::save_dict $out/${id}_boundary.tcldata [amf_dfx::boundary_snapshot]
    }
    if {[info exists dfx(native_cache_capture)] && $dfx(native_cache_capture)} {
        source [file join [file dirname [info script]] dfx_native_certify.tcl]
        amf_dfx::timed native_cache_certification {amf_native_certify::run $out}
    }
    amf_dfx::timed full_bitstream {write_bitstream $out/base_full.bit}
    foreach row $dfx(rps) {lassign $row id rp;amf_dfx::timed partial_bitstream_$id {write_bitstream -cell $rp $out/${id}_partial.bit}}
    close_design
    # The reusable incremental input contains no old RM implementation.
    if {$dfx(prepare_common_static_shell)} {amf_common_static::create $out/base_routed.dcp $out}
    puts AMF_FULL_DFX_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
