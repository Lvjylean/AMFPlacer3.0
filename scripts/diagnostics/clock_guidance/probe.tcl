# Independent clock-guidance test. Source DCP and all parent files are immutable.
lassign $argv parent helpers out mode
source $parent/joint/inputs/settings.tcl
source $helpers/dfx_joint_checks.tcl
set comparator [file join [file dirname [info script]] compare_placement.py]
set dfx(output) $out
set dfx(static_placement_audit) skip
file mkdir $out
proc save_text {path value} {set f [open $path w];puts $f $value;close $f}
set snapshot_serial 0
proc all_placement {} {
    global out snapshot_serial
    set prefix $out/placement_[incr snapshot_serial]
    set cs [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}]
    set names [get_property NAME $cs];set locs [get_property LOC $cs];set bels [get_property BEL $cs]
    set count [llength $cs]
    if {$count != [llength $names] || $count != [llength $locs] || $count != [llength $bels]} {error "Incomplete placement property vectors"}
    save_text $prefix.names [join $names \n]
    save_text $prefix.locs [join $locs \n]
    save_text $prefix.bels [join $bels \n]
    save_text $prefix.count $count
    puts "FULL_PLACEMENT_EXPORTED prefix=$prefix count=$count"
    return $prefix
}
proc compare_placement {before label} {
    global out comparator
    set after [all_placement]
    puts [exec python3 $comparator $before $after $out/$label]
}
proc clock_state {label} {
    global out
    set clocks [get_nets -quiet -of_objects [get_pins clock_buffer/O]]
    if {[llength $clocks]!=1} {error "Expected one clock-buffer output net"}
    set result {};set f [open $out/${label}_clock_properties.tsv w]
    foreach n $clocks {
        set name [get_property NAME $n]
        foreach property {TYPE IS_ROUTE_FIXED FIXED_ROUTE ROUTE_STATUS CLOCK_ROOT USER_CLOCK_ROOT} {
            set val [get_property $property $n]
            if {$property eq "FIXED_ROUTE"} {save_text $out/${label}_fixed_clock_route.tcldata $val;dict set result $name $val;set val "serialized_length=[string length $val]"}
            puts $f "$name\t$property\t$val"
        }
    }
    close $f
    return $result
}
proc main {} {
    global dfx parent helpers out mode
    if {$mode ni {verbose_baseline update_preserve}} {error "Unknown experiment mode"}
    set_param general.maxThreads 4
    puts "CLOCK_GUIDANCE_TEST mode=$mode place_design_calls=0 unroute_calls=0"
    help update_clock_routing
    amf_dfx::timed open_input {open_checkpoint $parent/joint/joint_amf_placed.dcp}
    dfx_clock_contract::assert_top before 1
    set signature [dfx_clock_contract::signature]
    amf_dfx::timed snapshot_all_placement {set before [all_placement]}
    set fixed_before [clock_state before]
    set requests {};set boundaries {}
    foreach row $dfx(rps) {
        lassign $row id rp pb
        dict set requests $id [joint::requested $id $rp]
        dict set boundaries $id [amf_dfx::read_dict [file join $dfx(common_static_metadata) [set id]_common_static_boundary.tcldata]]
    }
    set footprints [amf_dfx::read_dict $parent/joint/imported_rp_route_footprints.tcldata]
    if {$mode eq "update_preserve"} {
        amf_dfx::timed update_clock_routing {update_clock_routing -verbose}
        amf_dfx::timed verify_update_placement {compare_placement $before after_update}
        set fixed_after [clock_state after_update]
        save_text $out/after_update_fixed_clock_equality.tsv "exact\t[expr {$fixed_before eq $fixed_after}]"
        dfx_clock_contract::assert_unchanged $signature after_update
    }
    amf_dfx::timed route_verbose {route_design -verbose}
    amf_dfx::timed verify_routed_placement {compare_placement $before routed}
    set fixed_after [clock_state routed]
    save_text $out/routed_fixed_clock_equality.tsv "exact\t[expr {$fixed_before eq $fixed_after}]"
    dfx_clock_contract::assert_unchanged $signature routed
    amf_dfx::timed audit_routed {joint::audit routed $requests $boundaries $footprints}
    amf_dfx::timed write_result {write_checkpoint $out/result_routed.dcp}
    close_design
    amf_dfx::timed pr_verify_full {pr_verify -full_check $dfx(reference_checkpoint) $out/result_routed.dcp}
    save_text $out/pr_verify_passed.txt full_check_passed
    save_text $out/completed.txt "mode=$mode place_design_calls=0 unroute_calls=0"
}
if {[catch {main} message opts]} {save_text $out/error.txt "$message\n[dict get $opts -errorinfo]";puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
