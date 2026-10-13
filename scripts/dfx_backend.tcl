source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    open_checkpoint $out/linked.dcp
    amf_dfx::assert_context
    set before [amf_dfx::read_dict $out/static_snapshot.tcldata]
    amf_dfx::assert_same $before [amf_dfx::static_snapshot] "Before AMF import"
    set f [open $out/placement.tsv r]
    set requested [dict create]
    while {[gets $f line] >= 0} {
        lassign [split $line \t] c target
        if {![amf_dfx::active $c] || [dict exists $requested $c]} {error "Invalid active placement row: $line"}
        dict set requested $c $target
        # Strict import: no global unplace, no failed-cell fallback to new sites.
        place_cell [list $c $target]
    }
    close $f
    amf_dfx::assert_same $before [amf_dfx::static_snapshot] "After AMF import"
    write_checkpoint $out/imported.dcp
    amf_dfx::audit $out imported
    place_design
    write_checkpoint $out/placed.dcp
    amf_dfx::audit $out placed
    route_design
    write_checkpoint $out/routed.dcp
    amf_dfx::audit $out routed
    report_route_status -file $out/route_status.rpt
    amf_dfx::save_dict $out/final_boundary.tcldata [amf_dfx::boundary_snapshot]
    set f [open $out/placement_retention.tsv w]; puts $f "cell\trequested\tloc\tbel"
    dict for {c target} $requested { puts $f "$c\t$target\t[get_property LOC [get_cells $c]]\t[get_property BEL [get_cells $c]]" }
    close $f
    # Placement equality alone is insufficient: compare the static routing and
    # partition interfaces against the first routed configuration as well.
    pr_verify -full_check $dfx(reference_checkpoint) $out/routed.dcp
    set f [open $out/pr_verify_passed.txt w]; puts $f full_check_passed; close $f
    write_bitstream $out/full.bit
    write_bitstream -cell $dfx(active_partition) $out/partial.bit
    puts "AMF_DFX_BACKEND_COMPLETED"
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
