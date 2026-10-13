# Initial configuration only. Subsequent RMs must use the locked shell via dfx-run.
# bblock is read from the elaborated/synthesized hierarchy, not by regex over RTL.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    create_project -in_memory -part $dfx(part)
    foreach path $dfx(rtl_sources) {read_verilog -sv $path}
    synth_design -top $dfx(top) -part $dfx(part) -flatten_hierarchy none
    set marked {}
    foreach c [get_cells -hierarchical] {
        if {[lsearch -nocase -exact [list_property $c] bblock] >= 0 &&
            [string tolower [get_property bblock $c]] in {1 true yes}} {
            lappend marked [get_property NAME $c]
        }
    }
    if {[llength $marked] != 1} {error "DFX v1 requires exactly one bblock instance; found $marked"}
    if {[info exists dfx(active_partition)] && $dfx(active_partition) ne [lindex $marked 0]} {
        error "Configured RP disagrees with RTL bblock instance"
    }
    set dfx(active_partition) [lindex $marked 0]
    set rp [get_cells $dfx(active_partition)]
    set_property HD.RECONFIGURABLE true $rp
    set_property DONT_TOUCH true $rp
    foreach path $dfx(xdc_files) {read_xdc $path}
    create_pblock $dfx(pblock)
    resize_pblock [get_pblocks $dfx(pblock)] -add $dfx(pblock_ranges)
    add_cells_to_pblock [get_pblocks $dfx(pblock)] $rp
    set_property SNAPPING_MODE ON [get_pblocks $dfx(pblock)]
    set_property RESET_AFTER_RECONFIG true [get_pblocks $dfx(pblock)]
    set_property EXCLUDE_PLACEMENT true [get_pblocks $dfx(pblock)]
    amf_dfx::assert_context
    amf_dfx::save_dict $out/discovered_partition.tcldata [dict create \
        active_partition $dfx(active_partition) pblock $dfx(pblock) part $dfx(part)]
    opt_design
    write_checkpoint $out/base_opt.dcp
    place_design
    write_checkpoint $out/base_placed.dcp
    route_design
    write_checkpoint $out/base_routed.dcp
    report_route_status -file $out/base_route_status.rpt
    report_drc -file $out/base_drc.rpt
    report_timing_summary -delay_type min_max -report_unconstrained -file $out/base_timing.rpt
    write_bitstream $out/base_full.bit
    write_bitstream -cell $dfx(active_partition) $out/base_partial.bit
    update_design -cell $dfx(active_partition) -black_box
    lock_design -level routing
    write_checkpoint $out/static_locked.dcp
    puts "AMF_DFX_SHELL_COMPLETED"
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
