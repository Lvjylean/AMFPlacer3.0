# Optimize a complete synthesized RM in isolation; no static/RP placeholders.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
proc port_signature {} {
    set rows {}
    foreach p [get_ports] {lappend rows [list [get_property NAME $p] [get_property DIRECTION $p]]}
    return [lsort $rows]
}
proc check_clock {} {
    global dfx
    set c [get_clocks $dfx(clock_name)]
    if {[llength $c]!=1 || abs([get_property PERIOD $c]-$dfx(clock_period_ns))>1e-6} {error "OOC RM clock mismatch"}
}
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    amf_dfx::timed open_rm_synthesis {open_checkpoint $dfx(rm_synth_checkpoint)}
    check_clock
    set ports [port_signature]
    if {![llength $ports]} {error "Empty RM interface"}
    amf_dfx::save_dict $out/ports_before.tcldata $ports
    amf_dfx::timed rm_ooc_opt {opt_design}
    check_clock
    if {$ports ne [port_signature]} {error "Optimization changed RM interface"}
    if {[llength [get_cells -hierarchical -filter {IS_BLACKBOX}]]} {error "Unresolved black boxes in optimized RM"}
    amf_dfx::save_dict $out/ports_after.tcldata [port_signature]
    amf_dfx::timed write_optimized_rm {write_checkpoint $out/rm_optimized.dcp}
    set f [open $out/validation.tsv w]
    puts $f "port_bits\t[llength $ports]"
    puts $f "interface_exact\t1"
    puts $f "unresolved_blackboxes\t0"
    puts $f "clock_period_ns\t[get_property PERIOD [get_clocks $dfx(clock_name)]]"
    puts $f "primitive_cells\t[llength [get_cells -hierarchical -filter {IS_PRIMITIVE}]]"
    puts $f "opt_executed\t1"
    close $f
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
