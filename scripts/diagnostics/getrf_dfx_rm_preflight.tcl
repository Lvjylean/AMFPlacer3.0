# OOC resource and changed-netlist check; no placement or routing.
set fixture [file normalize [lindex $argv 0]]
set out [file normalize [lindex $argv 1]]
file mkdir $out
set_param general.maxThreads 4
proc run {} {
    global fixture out
    foreach variant {v0 v1} {
        set dir $fixture/$variant
        set dest $out/$variant
        file mkdir $dest
        create_project rm_$variant $dest/project -part xcu250-figd2104-2L-e
        foreach f [lsort [glob $dir/*_ip.tcl]] {source $f}
        read_verilog [lsort [glob $dir/*.v]]
        set t [clock milliseconds]
        synth_design -mode out_of_context -top getrf_dfx_rm -part xcu250-figd2104-2L-e -flatten_hierarchy none
        create_clock -name dfx_clk -period 10.000 [get_ports ap_clk]
        set f [open $dest/synthesis_seconds.txt w]; puts $f [expr {([clock milliseconds]-$t)/1000.0}];close $f
        write_checkpoint $dest/rm.dcp
        write_verilog $dest/synth.v
        report_utilization -hierarchical -file $dest/utilization.rpt
        set f [open $dest/cells.tsv w]; puts $f "name\tref\tinit"
        foreach c [lsort [get_cells -hierarchical -filter {IS_PRIMITIVE}]] {
            puts $f "[get_property NAME $c]\t[get_property REF_NAME $c]\t[get_property -quiet INIT $c]"
        }
        close $f
        set f [open $dest/ports.tsv w];puts $f "port\tdirection"
        foreach p [lsort [get_ports *]] {puts $f "$p\t[get_property DIRECTION $p]"}
        close $f
        set f [open $dest/cr_capacity.tsv w];puts $f "cr\tslr\tsite_type\tcount"
        foreach cr {CLOCKREGION_X2Y1 CLOCKREGION_X2Y5} {
            set counts [dict create]
            foreach s [get_sites -of_objects [get_clock_regions $cr]] {dict incr counts [get_property SITE_TYPE $s]}
            foreach type [lsort [dict keys $counts]] {
                puts $f "$cr\t[get_slrs -of_objects [get_clock_regions $cr]]\t$type\t[dict get $counts $type]"
            }
        }
        close $f
        close_project
    }
    puts GETRF_DFX_RM_PREFLIGHT_COMPLETED
}
if {[catch {run} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0

