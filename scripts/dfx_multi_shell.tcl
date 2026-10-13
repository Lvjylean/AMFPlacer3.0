source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    create_project -in_memory -part $dfx(part)
    foreach f $dfx(rtl_sources) {read_verilog -sv $f}
    amf_dfx::timed synthesis {synth_design -top $dfx(top) -part $dfx(part) -flatten_hierarchy none}
    set marked {}; set expected {}
    foreach c [get_cells -hierarchical] {
        if {[lsearch -nocase -exact [list_property $c] bblock]>=0 && [string tolower [get_property bblock $c]] in {true 1 yes}} {lappend marked [get_property NAME $c]}
    }
    foreach row $dfx(rps) {lappend expected [lindex $row 1]}
    if {[lsort $marked] ne [lsort $expected]} {error "RTL bblock set disagrees with configured partitions: $marked vs $expected"}
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
    foreach f $dfx(xdc_files) {read_xdc $f}
    amf_multi::validate_regions
    amf_dfx::timed opt {opt_design}
    amf_dfx::timed vivado_place {place_design}
    write_checkpoint $out/base_placed.dcp
    amf_dfx::timed vivado_route {route_design}
    write_checkpoint $out/base_routed.dcp
    amf_multi::reports $out/base
    amf_dfx::save_dict $out/static_snapshot.tcldata [amf_multi::static_snapshot]
    foreach row $dfx(rps) {
        lassign $row id rp pb
        set dfx(active_partition) $rp
        amf_dfx::save_dict $out/${id}_boundary.tcldata [amf_dfx::boundary_snapshot]
    }
    amf_dfx::timed full_bitstream {write_bitstream $out/base_full.bit}
    close_design
    foreach row $dfx(rps) {
        lassign $row id rp pb
        open_checkpoint $out/base_routed.dcp
        update_design -cell $rp -black_box
        lock_design -level routing
        write_checkpoint $out/${id}_locked.dcp
        close_design
    }
    puts AMF_MULTI_SHELL_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
