source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    if {[info exists dfx(resume_synthesis_checkpoint)]} {
        amf_dfx::timed reopen_synthesis {open_checkpoint $dfx(resume_synthesis_checkpoint)}
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
    amf_dfx::timed synthesis_utilization {report_utilization -hierarchical -file $out/base_synth_utilization.rpt}
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
    amf_dfx::timed opt {opt_design}
    amf_dfx::timed write_opt_dcp {write_checkpoint $out/base_opt.dcp}
    amf_dfx::timed vivado_place {place_design}
    amf_dfx::timed write_placed_dcp {write_checkpoint $out/base_placed.dcp}
    report_timing_summary -delay_type min_max -file $out/base_placed_timing.rpt
    amf_dfx::timed vivado_route {route_design}
    amf_dfx::timed write_routed_dcp {write_checkpoint $out/base_routed.dcp}
    amf_dfx::timed reports {amf_multi::reports $out/base}
    report_utilization -file $out/base_utilization.rpt
    amf_dfx::save_dict $out/static_snapshot.tcldata [amf_multi::static_snapshot]
    foreach row $dfx(rps) {
        lassign $row id rp pb
        set dfx(active_partition) $rp
        amf_dfx::save_dict $out/${id}_boundary.tcldata [amf_dfx::boundary_snapshot]
    }
    amf_dfx::timed full_bitstream {write_bitstream $out/base_full.bit}
    foreach row $dfx(rps) {
        lassign $row id rp pb
        amf_dfx::timed partial_bitstream_$id {write_bitstream -cell $rp $out/${id}_partial.bit}
    }
    close_design
    if {[info exists dfx(prepare_worker_shells)] && !$dfx(prepare_worker_shells)} {
        puts AMF_MULTI_SHELL_COMPLETED
        return
    }
    foreach row $dfx(rps) {
        lassign $row id rp pb
        amf_dfx::timed reopen_for_lock_$id {open_checkpoint $out/base_routed.dcp}
        update_design -cell $rp -black_box
        lock_design -level routing
        amf_dfx::timed write_locked_dcp_$id {write_checkpoint $out/${id}_locked.dcp}
        close_design
    }
    puts AMF_MULTI_SHELL_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
