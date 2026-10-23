# Read a routed DCP and change only timing constraints in memory. No P&R or DCP writes.
if {$argc != 2} { error "Usage: vivado -mode batch -source SCRIPT -tclargs ROUTED_DCP OUTPUT_DIR" }
set source_dcp [lindex $argv 0]
set output_dir [file normalize [lindex $argv 1]]
file mkdir $output_dir
set_param general.maxThreads 4
open_checkpoint $source_dcp
if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} { error "Unexpected part" }
set clock_names [lsort -dictionary [get_property NAME [get_clocks *]]]
if {[llength $clock_names] != 30} { error "Expected 30 clocks" }
foreach name $clock_names {
    set clock [get_clocks $name]
    set port [get_ports -quiet $name]
    if {[llength $port] != 1} { error "Missing clock port $name" }
    if {[get_property NAME [get_clocks -of_objects $port]] ne $name} { error "Clock source mismatch $name" }
    if {abs([get_property PERIOD $clock] - 10.0) > 0.000001} { error "Unexpected period $name" }
    set waveform [get_property WAVEFORM $clock]
    if {[llength $waveform] != 2 || abs([lindex $waveform 0]) > 0.000001 || abs([lindex $waveform 1] - 5.0) > 0.000001} {
        error "Unexpected waveform $name: $waveform"
    }
}
set metrics [open [file join $output_dir metrics.tsv] w]
puts $metrics "period_ns\tdelay_type\tslack_ns\tstartpoint\tendpoint"
foreach period {10 20} {
    if {$period == 20} {
        foreach name $clock_names {
            create_clock -name $name -period 20.000 -waveform {0.000 10.000} [get_ports $name]
        }
    }
    report_clocks -file [file join $output_dir clocks_${period}ns.rpt]
    foreach delay_type {min max} {
        set path [get_timing_paths -delay_type $delay_type -max_paths 1]
        if {[llength $path] != 1} { error "Expected a timing path" }
        set slack [get_property SLACK $path]
        set startpoint [get_property STARTPOINT_PIN $path]
        set endpoint [get_property ENDPOINT_PIN $path]
        puts $metrics "$period\t$delay_type\t$slack\t$startpoint\t$endpoint"
        flush $metrics
        report_timing -delay_type $delay_type -max_paths 1 -path_type full_clock_expanded -file [file join $output_dir worst_${delay_type}_${period}ns.rpt]
    }
}
close $metrics
close_design
puts "PERIOD_SENSITIVITY_COMPLETE"
