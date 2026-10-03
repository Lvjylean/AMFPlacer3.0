# Preserve the netlist and fixed placement; remove two incorrectly timed controls.
if {$argc != 2} {error "Expected input DCP and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
source [file join [file dirname [info script]] classify_ispd_global_buffers.tcl]
if {[catch {
    open_checkpoint $input
    if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {error "Unexpected part"}
    set before_cells [llength [get_cells -hier -filter {IS_PRIMITIVE}]]
    set before_nets [llength [get_nets -hier]]
    set true_buffers [ispd_clock_buffers [file join $output global_buffer_sinks.tsv]]
    if {[llength $true_buffers] != 30} {error "Expected 30 actual clock buffers"}
    foreach buffer {BUFG_inst0 BUFG_inst1} clock_name {ip_300 ip_301} {
        if {$buffer in $true_buffers} {error "Control buffer unexpectedly drives clock pins"}
        set c [get_clocks $clock_name]
        if {[llength $c] != 1 || [get_property PERIOD $c] != 10.0} {error "Unexpected control clock"}
    }
    # The source timing XDC contains only the 32 clocks, without exceptions/delays.
    # Vivado 2024.2 exposes reset_timing, not a delete_clocks command.
    reset_timing
    foreach buffer $true_buffers {
        set roots [all_fanin -flat -startpoints_only -to [get_pins $buffer/I]]
        if {[llength $roots] != 1 || [get_property CLASS $roots] ne "port"} {error "Unexpected clock root"}
        create_clock -name [get_property NAME $roots] -period 10.0 $roots
    }
    if {[llength [get_clocks *]] != 30} {error "Expected 30 remaining clocks"}
    foreach buffer $true_buffers {
        set c [get_clocks -of_objects [get_pins $buffer/O]]
        if {[llength $c] != 1 || [get_property PERIOD $c] != 10.0} {error "Missing 10 ns clock on $buffer"}
    }
    foreach port {ip_300 ip_301} {
        if {[llength [get_clocks -quiet -of_objects [get_ports $port]]]} {error "Control still timed as clock"}
    }
    if {$before_cells != [llength [get_cells -hier -filter {IS_PRIMITIVE}]] ||
        $before_nets != [llength [get_nets -hier]]} {error "Structural inventory changed"}
    report_clocks -file [file join $output clocks.rpt]
    check_timing -verbose -file [file join $output check_timing.rpt]
    write_xdc -type timing [file join $output timing.xdc]
    write_checkpoint [file join $output amf_input_30clocks.dcp]
    set out [open [file join $output validation.tsv] w]
    puts $out "primitive_cells\t$before_cells\nhierarchical_nets\t$before_nets\ntrue_clock_buffers\t30\nglobal_buffers\t32\nperiod_ns\t10\nremoved_control_clocks\tip_300,ip_301"
    close $out
    puts "ISPD_TIMING_CORRECTION_PASSED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
