# Independent, immutable-input backend experiment. Never unplace or unlock cells.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
namespace eval probe {
    variable last_boundary
    variable requested [dict create]
    variable allowed [dict create]
    variable blocked [dict create]
    proc load_inputs {} {
        global dfx
        variable requested; variable allowed; variable blocked
        set f [open $dfx(placement_table) r]
        while {[gets $f line] >= 0} {
            lassign [split $line \t] cell target
            if {![amf_dfx::active $cell] || $target eq "" || [dict exists $requested $cell]} {error "Invalid placement row: $line"}
            dict set requested $cell $target
        }
        close $f
        foreach {path var} [list $dfx(allowed_sites) allowed $dfx(blocked_sites) blocked] {
            set f [open $path r]
            while {[gets $f line] >= 0} {if {$line ne ""} {dict set $var $line 1}}
            close $f
        }
        if {[dict size $requested] != $dfx(expected_cells)} {error "Unexpected AMF export count"}
    }
    proc positions {stage require_exact} {
        global dfx
        variable requested; variable allowed; variable blocked
        set f [open $dfx(output)/${stage}_retention.tsv w]
        puts $f "cell\trequested\tloc\tbel\tloc_fixed\tbel_fixed"
        set moved 0; set outside 0; set unfixed 0
        set activeFilter [format {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME != GND && REF_NAME != VCC && NAME =~ "%s/*"} $dfx(active_partition)]
        set activeCount [llength [get_cells -hierarchical -filter $activeFilter]]
        if {$require_exact && $activeCount != [dict size $requested]} {error "Imported RM coverage mismatch: $activeCount"}
        dict for {name target} $requested {
            set c [get_cells -quiet $name]
            if {[llength $c] != 1} {error "Missing or ambiguous AMF cell: $name"}
            set loc [get_property LOC $c]; set bel [get_property BEL $c]
            set lf [get_property IS_LOC_FIXED $c]; set bf [get_property IS_BEL_FIXED $c]
            puts $f "$name\t$target\t$loc\t$bel\t$lf\t$bf"
            if {$loc eq "" || $bel eq ""} {error "Unplaced AMF cell: $name"}
            if {"$loc/[lindex [split $bel .] end]" ne $target} {incr moved}
            if {![dict exists $allowed $loc] || [dict exists $blocked $loc]} {incr outside}
            if {!$lf || !$bf} {incr unfixed}
        }
        close $f
        puts "AMF_DFX_PROBE_POSITIONS stage=$stage cells=[dict size $requested] active_primitives=$activeCount moved=$moved outside=$outside unfixed=$unfixed"
        if {$outside || ($require_exact && $moved)} {error "Placement audit failed at $stage"}
    }
    proc boundary {stage before} {
        global dfx
        set after [amf_dfx::boundary_snapshot]
        amf_dfx::save_dict $dfx(output)/${stage}_boundary.tcldata $after
        variable last_boundary
        set comparison [dfx_boundary::compare $before $after]
        if {[info exists last_boundary]} {
            # Preserve locations assigned by the preceding stage as well.
            dfx_boundary::compare $last_boundary $after
        }
        set last_boundary $after
        amf_dfx::save_dict $dfx(output)/${stage}_boundary_comparison.tcldata $comparison
        set f [open $dfx(output)/boundary_assignment_counts.tsv a]
        puts $f "$stage\t[dict get $comparison assigned_preserved]\t[dict size [dict get $comparison newly_assigned]]\t[dict get $comparison unassigned_remaining]\t[dict get $comparison snapshots_equal]"
        close $f
        set f [open $dfx(output)/boundary_checks.tsv a]; puts $f "$stage\tpassed"; close $f
    }
}
proc main {} {
    global dfx
    if {$dfx(mode) ni {route-only quick}} {error "Unsupported probe mode"}
    set_param general.maxThreads $dfx(vivado_threads)
    set out $dfx(output)
    amf_dfx::timed open_imported {open_checkpoint $dfx(imported_checkpoint)}
    amf_dfx::timed input_checks {
        amf_dfx::assert_context
        probe::load_inputs
        probe::positions input 1
        set boundary [amf_dfx::boundary_snapshot]
        amf_dfx::save_dict $out/input_boundary.tcldata $boundary
    }
    if {$dfx(mode) eq "quick"} {
        amf_dfx::timed vivado_place {place_design -directive Quick}
        amf_dfx::timed after_place_checks {probe::positions placed 0; probe::boundary placed $boundary}
        amf_dfx::timed write_placed {write_checkpoint $out/reports/placed.dcp}
    } else {
        puts "AMF_DFX_PROBE_PLACEMENT_SKIPPED mode=route-only"
    }
    # No pre-route report_timing/report_drc duplication: route_design performs its checks.
    amf_dfx::timed vivado_route {route_design}
    amf_dfx::timed write_routed {write_checkpoint $out/reports/routed.dcp}
    amf_dfx::timed after_route_checks {probe::positions routed 0; probe::boundary routed $boundary}
    amf_dfx::timed audit_routed {amf_dfx::audit $out routed}
    amf_dfx::timed route_status {report_route_status -file $out/route_status.rpt}
    # Full static routing/interface comparison, without repeated 850k-cell Tcl snapshots.
    amf_dfx::timed pr_verify {pr_verify -full_check $dfx(reference_checkpoint) $out/reports/routed.dcp}
    set f [open $out/pr_verify_passed.txt w]; puts $f full_check_passed; close $f
    amf_dfx::timed write_rm_routed {write_checkpoint -cell $dfx(active_partition) $out/reports/rm_routed.dcp}
    amf_dfx::timed full_bitstream {write_bitstream $out/reports/full.bit}
    amf_dfx::timed partial_bitstream {write_bitstream -cell $dfx(active_partition) $out/reports/partial.bit}
    puts AMF_DFX_PLACE_MODE_PROBE_COMPLETED
}
if {[catch {main} msg opts]} {
    set f [open $dfx(output)/failure.txt w]; puts $f $msg; puts $f [dict get $opts -errorinfo]; close $f
    puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2
}
exit 0
