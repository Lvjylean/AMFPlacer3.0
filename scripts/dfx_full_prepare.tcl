source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_static_containment.tcl]
source [file join [file dirname [info script]] dfx_static_membership.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    if {[info exists dfx(resume_opt_checkpoint)]} {
        amf_dfx::timed reopen_opt {open_checkpoint $dfx(resume_opt_checkpoint)}
        amf_multi::validate_regions
        amf_static::recover_validate
        amf_dfx::timed static_membership_recovered_opt {amf_static_membership::enforce recovered_opt 1}
        source [file join [file dirname [info script]] dfx_full_input.tcl]
        source [file join [file dirname [info script]] dfx_full_input_batch.tcl]
        amf_dfx::timed full_amf_input {amf_full::export_batched $out}
    if {[info exists dfx(full_edge_policy)] && $dfx(full_edge_policy)} {
        source [file join [file dirname [info script]] dfx_native_certify.tcl]
        amf_dfx::timed full_input_resource_inventory {amf_native_certify::resources $out}
    }
        if {[info exists dfx(native_cache_capture)] && $dfx(native_cache_capture)} {
            source [file join [file dirname [info script]] dfx_native_export.tcl]
            amf_dfx::timed native_full_interfaces {amf_native_export::full_interfaces $out}
        }
        puts AMF_FULL_DFX_PREPARED
        return
    }
    if {[info exists dfx(resume_synthesis_checkpoint)]} {
        amf_dfx::timed reopen_synthesis {open_checkpoint $dfx(resume_synthesis_checkpoint)}
        # Explicit user implementation XDC replaces the checkpoint clock.
        # The usual context checks below verify its actual period.
        amf_dfx::timed implementation_constraints {foreach f $dfx(xdc_files) {read_xdc $f}}
    } else {
        if {[llength $dfx(ip_scripts)]} {
            create_project dfx_shell $out/ip_project -part $dfx(part)
            amf_dfx::timed ip_generation {foreach f $dfx(ip_scripts) {source $f}}
        } else {
            create_project -in_memory -part $dfx(part)
        }
        foreach f $dfx(rtl_sources) {read_verilog -sv $f}
        foreach f $dfx(xdc_files) {read_xdc $f}
        amf_dfx::timed synthesis {synth_design -top $dfx(top) -part $dfx(part) -flatten_hierarchy none}
        amf_dfx::timed write_synth_dcp {write_checkpoint $out/base_synth.dcp}
    }
    # Descriptive utilization is generated after main-flow completion.
    set marked {}; set expected {}
    amf_dfx::timed bblock_discovery {
        # Avoid enumerating every property of every cell in large netlists.
        # Glob character classes preserve the previous case-insensitive values.
        set cells [get_cells -hierarchical -quiet -filter {bblock =~ [tT][rR][uU][eE] || bblock == 1 || bblock =~ [yY][eE][sS]}]
        foreach c $cells {lappend marked [get_property NAME $c]}
        foreach row $dfx(rps) {lappend expected [lindex $row 1]}
        if {[lsort $marked] ne [lsort $expected]} {error "RTL bblock set disagrees with configured partitions: $marked vs $expected"}
    }
    amf_dfx::timed pblock_configuration {
    foreach row $dfx(rps) {
        lassign $row id rp pb ranges slr
        set_property HD.RECONFIGURABLE true [get_cells $rp]
        set_property DONT_TOUCH true [get_cells $rp]
        create_pblock $pb
        resize_pblock [get_pblocks $pb] -add $ranges
        add_cells_to_pblock [get_pblocks $pb] [get_cells $rp]
        set_property SNAPPING_MODE ON [get_pblocks $pb]
        set_property IS_SOFT false [get_pblocks $pb]
        set_property EXCLUDE_PLACEMENT true [get_pblocks $pb]
        set_property CONTAIN_ROUTING true [get_pblocks $pb]
    }
    amf_multi::validate_regions
    }
    amf_dfx::timed static_containment_preflight {amf_static::configure_validate}
    amf_dfx::timed opt {opt_design}
    amf_dfx::timed static_membership_post_opt {amf_static_membership::enforce post_opt 1}
    amf_dfx::timed write_opt_dcp {write_checkpoint $out/base_opt.dcp}
    source [file join [file dirname [info script]] dfx_full_input.tcl]
        source [file join [file dirname [info script]] dfx_full_input_batch.tcl]
    amf_dfx::timed full_amf_input {amf_full::export_batched $out}
    if {[info exists dfx(full_edge_policy)] && $dfx(full_edge_policy)} {
        source [file join [file dirname [info script]] dfx_native_certify.tcl]
        amf_dfx::timed full_input_resource_inventory {amf_native_certify::resources $out}
    }
    if {[info exists dfx(native_cache_capture)] && $dfx(native_cache_capture)} {
        source [file join [file dirname [info script]] dfx_native_export.tcl]
        amf_dfx::timed native_full_interfaces {amf_native_export::full_interfaces $out}
    }
    puts AMF_FULL_DFX_PREPARED
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
