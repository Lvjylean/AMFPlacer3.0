# A self-contained U250 DFX shell for integration tests. Does not program hardware.
# Usage: vivado -mode batch -source dfx_fixture.tcl -tclargs <new-run-dir>
set script_root [file dirname [file normalize [info script]]]
proc main {out} {
    global script_root
    set fixture [file normalize "$script_root/../tests/dfx/u250_small"]
    file mkdir $out
    set_param general.maxThreads 4
    set part xcu250-figd2104-2L-e
    foreach variant {a b} {
        create_project -in_memory -part $part
        read_verilog $fixture/rm_$variant.v
        synth_design -top rm -mode out_of_context -part $part
        write_checkpoint $out/rm_$variant.dcp
        close_project
    }
    create_project -in_memory -part $part
    read_verilog [list $fixture/static_top.v $fixture/rm_a.v]
    synth_design -top static_top -part $part -flatten_hierarchy none
    set rp [get_cells rm_inst]
    if {[llength $rp] != 1 || [string tolower [get_property bblock $rp]] ni {1 true}} {
        error "Verilog bblock attribute was not preserved on rm_inst"
    }
    set_property HD.RECONFIGURABLE true $rp
    create_clock -name dfx_clk -period 10.000 [get_pins jtag/TCK]
    create_pblock pblock_rm
    resize_pblock [get_pblocks pblock_rm] -add {CLOCKREGION_X1Y4:CLOCKREGION_X1Y4}
    add_cells_to_pblock [get_pblocks pblock_rm] $rp
    set_property SNAPPING_MODE ON [get_pblocks pblock_rm]
    set_property RESET_AFTER_RECONFIG true [get_pblocks pblock_rm]
    set_property EXCLUDE_PLACEMENT true [get_pblocks pblock_rm]
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
    write_bitstream -cell rm_inst $out/base_partial.bit
    update_design -cell rm_inst -black_box
    lock_design -level routing
    write_checkpoint $out/static_locked.dcp
    close_project
    puts "AMF_DFX_FIXTURE_COMPLETED"
}
if {[catch {main [file normalize [lindex $argv 0]]} message options]} {
    puts stderr "AMF_DFX_FIXTURE_ERROR: $message"
    puts stderr [dict get $options -errorinfo]
    exit 2
}
exit 0
