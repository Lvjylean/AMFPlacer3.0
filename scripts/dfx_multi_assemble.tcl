source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    # Snapshot each fully routed worker before assembling only its own module.
    set placements [dict create]; set routes [dict create]; set footprints [dict create]
    foreach row $dfx(rps) {
        lassign $row id rp pb
        set worker $dfx(workers)/$id
        open_checkpoint $worker/routed.dcp
        amf_dfx::save_dict $out/${id}_placement.tcldata [amf_multi::module_snapshot $rp]
        amf_dfx::save_dict $out/${id}_routing.tcldata [amf_multi::module_routing $rp]
        dict set placements $id [amf_dfx::read_dict $out/${id}_placement.tcldata]
        dict set routes $id [amf_dfx::read_dict $out/${id}_routing.tcldata]
        dict set footprints $id [amf_multi::route_footprint $rp]
        close_design
    }
    amf_dfx::timed open_reference {open_checkpoint $dfx(reference_checkpoint)}
    set static [amf_multi::static_snapshot]
    set boundaries [dict create]
    foreach row $dfx(rps) {
        lassign $row id rp pb
        set dfx(active_partition) $rp
        dict set boundaries $id [amf_dfx::boundary_snapshot]
        update_design -cell $rp -black_box
    }
    lock_design -level routing
    foreach row $dfx(rps) {
        lassign $row id rp pb
        amf_dfx::timed import_$id {read_checkpoint -strict -cell $rp $dfx(workers)/$id/rm_routed.dcp}
    }
    # Local constraints are not carried by write_checkpoint -cell. Reapply the
    # supplied full-hierarchy XDCs (empty only when there are no RM-local constraints).
    foreach f $dfx(assembly_xdc_files) {read_xdc $f}
    amf_multi::validate_regions
    amf_dfx::assert_same $static [amf_multi::static_snapshot] "Assembly static placement"
    set f [open $out/module_preservation.tsv w]
    puts $f "id\tprimitive_count\thierarchical_net_count\tplacement_equal\tfootprint_routing_equal\tboundary_equal"
    foreach row $dfx(rps) {
        lassign $row id rp pb
        set dfx(active_partition) $rp
        amf_dfx::assert_same [dict get $placements $id] [amf_multi::module_snapshot $rp] "Module placement $id"
        if {[dict get $footprints $id] ne [amf_multi::route_footprint $rp]} {error "Routing footprint changed for $id"}
        amf_dfx::assert_same [dict get $routes $id] [amf_multi::module_routing $rp] "Module routing $id"
        amf_dfx::assert_same [dict get $boundaries $id] [amf_dfx::boundary_snapshot] "Partition boundary $id"
        set allowed [get_sites -of_objects [get_pblocks $pb]]
        dict for {c value} [amf_multi::module_snapshot $rp] {
            if {[lsearch -exact $allowed [lindex $value 1]]<0} {error "Assembled cell outside Pblock: $c"}
        }
        puts $f "$id\t[dict size [dict get $placements $id]]\t[dict size [dict get $routes $id]]\t1\t1\t1"
    }
    close $f
    # No place_design/route_design here: assembly must preserve both worker results.
    amf_dfx::timed write_assembled {write_checkpoint $out/assembled_routed.dcp}
    amf_dfx::timed reports {amf_multi::reports $out/assembled}
    amf_dfx::timed pr_verify {pr_verify -full_check $dfx(reference_checkpoint) $out/assembled_routed.dcp}
    set f [open $out/pr_verify_passed.txt w]; puts $f full_check_passed; close $f
    report_utilization -slr -file $out/utilization.rpt
    report_timing -max_paths 100 -path_type full_clock_expanded -file $out/critical_paths.rpt
    set f [open $out/cross_slr_nets.tsv w]; puts $f "net\tslrs"
    foreach n [get_nets -hierarchical] {
        set cells [get_cells -quiet -of_objects [get_pins -quiet -leaf -of_objects $n]]
        if {![llength $cells]} {continue}
        set slrs [lsort -unique [get_slrs -quiet -of_objects $cells]]
        if {[llength $slrs]>1} {puts $f "$n\t$slrs"}
    }
    close $f
    amf_dfx::timed full_bitstream {write_bitstream $out/full.bit}
    foreach row $dfx(rps) {
        lassign $row id rp pb
        amf_dfx::timed partial_$id {write_bitstream -cell $rp $out/${id}_partial.bit}
    }
    puts AMF_MULTI_ASSEMBLY_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
