source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_import.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    amf_dfx::timed open_linked {open_checkpoint $out/linked.dcp}
    amf_dfx::assert_context
    amf_dfx::check_static_placement $out/static_snapshot.tcldata before_amf_import
    set f [open $out/placement.tsv r]
    set requested [dict create]
    while {[gets $f line] >= 0} {
        lassign [split $line \t] c target
        if {![amf_dfx::active $c] || [dict exists $requested $c]} {error "Invalid active placement row: $line"}
        dict set requested $c $target
    }
    close $f
    # AMF has supplied a new legal packing. Synthesis LUT pairing must not
    # silently move another active cell while importing its requested BEL.
    amf_dfx::timed reset_active_lut_groups {amf_dfx::reset_active_lut_groups $out $requested}
    amf_dfx::timed import_amf_placement {
        dict for {c target} $requested {
            # Abort on failed placement; never silently fall back to native placement.
            place_cell [list $c $target]
        }
    }
    set f [open $out/allowed_sites.txt r]; set allowed [split [string trim [read $f]] \n]; close $f
    set f [open $out/blocked_sites.txt r]; set blocked [split [string trim [read $f]] \n]; close $f
    set remapped 0
    set f [open $out/import_retention.tsv w]; puts $f "cell\trequested\tloc\tbel"
    dict for {c target} $requested {
        set cell [get_cells $c]
        set loc [get_property LOC $cell]; set bel [get_property BEL $cell]
        puts $f "$c\t$target\t$loc\t$bel"
        if {[lsearch -exact $allowed $loc] < 0 || [lsearch -exact $blocked $loc] >= 0} {
            error "AMF import placed RM cell outside its available sites: $c ($loc)"
        }
        if {"$loc/[lindex [split $bel .] end]" ne $target} {incr remapped}
    }
    close $f
    puts "AMF_DFX_IMPORT_AUDITED count=[dict size $requested] native_remaps=$remapped"
    amf_dfx::check_static_placement $out/static_snapshot.tcldata after_amf_import
    amf_dfx::timed write_imported {write_checkpoint $out/imported.dcp}
    amf_dfx::timed audit_imported {amf_dfx::audit $out imported}
    amf_dfx::timed vivado_place {place_design}
    amf_dfx::timed write_placed {write_checkpoint $out/placed.dcp}
    amf_dfx::timed audit_placed {amf_dfx::audit $out placed}
    amf_dfx::timed vivado_route {route_design}
    amf_dfx::timed write_routed {write_checkpoint $out/routed.dcp}
    amf_dfx::timed audit_routed {amf_dfx::audit $out routed}
    report_route_status -file $out/route_status.rpt
    amf_dfx::save_dict $out/final_boundary.tcldata [amf_dfx::boundary_snapshot]
    set f [open $out/placement_retention.tsv w]; puts $f "cell\trequested\tloc\tbel"
    dict for {c target} $requested { puts $f "$c\t$target\t[get_property LOC [get_cells $c]]\t[get_property BEL [get_cells $c]]" }
    close $f
    # Placement equality alone is insufficient: compare the static routing and
    # partition interfaces against the first routed configuration as well.
    amf_dfx::timed pr_verify {pr_verify -full_check $dfx(reference_checkpoint) $out/routed.dcp}
    set f [open $out/pr_verify_passed.txt w]; puts $f full_check_passed; close $f
    # Also preserve the routed module for multi-RP assembly. The full design remains the audit reference.
    amf_dfx::timed write_rm_routed {write_checkpoint -cell $dfx(active_partition) $out/rm_routed.dcp}
    amf_dfx::timed full_bitstream {write_bitstream $out/full.bit}
    amf_dfx::timed partial_bitstream {write_bitstream -cell $dfx(active_partition) $out/partial.bit}
    puts "AMF_DFX_BACKEND_COMPLETED"
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
