# Per-RP synthesis in its own process/project, before incremental AMF placement.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    create_project rm $out/ip_project -part $dfx(part)
    amf_dfx::timed ip_generation {foreach f $dfx(ip_scripts) {source $f}}
    foreach f $dfx(rtl_sources) {read_verilog -sv $f}
    set f [open $out/clock.xdc w]
    puts $f [format {create_clock -name %s -period %.6f [get_ports %s]} $dfx(clock_name) $dfx(clock_period_ns) $dfx(rm_clock_port)]
    close $f
    read_xdc $out/clock.xdc
    amf_dfx::timed rm_synthesis {synth_design -mode out_of_context -top $dfx(rm_top) -part $dfx(part) -flatten_hierarchy none}
    set clk [get_clocks $dfx(clock_name)]
    if {[llength $clk]!=1 || abs([get_property PERIOD $clk]-$dfx(clock_period_ns))>1e-6} {error "RM clock mismatch"}
    amf_dfx::timed write_rm_dcp {write_checkpoint $out/rm.dcp}
    report_utilization -hierarchical -file $out/utilization.rpt
    set f [open $out/cells.tsv w];puts $f "name\tref\tinit"
    foreach c [lsort [get_cells -hierarchical -filter {IS_PRIMITIVE}]] {puts $f "$c\t[get_property REF_NAME $c]\t[get_property -quiet INIT $c]"}
    close $f
    puts AMF_RM_SYNTHESIS_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0

