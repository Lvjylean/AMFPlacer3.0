# Inspect an original public checkpoint. No placement, routing, or checkpoint writes.
if {$argc != 2} { error "Expected DCP and new output directory" }
set input [lindex $argv 0]
set out [lindex $argv 1]
if {[file exists $out]} { error "Refusing to overwrite $out" }
file mkdir $out
set_param general.maxThreads 4
if {[catch {
    set start [clock milliseconds]
    open_checkpoint $input
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE && REF_NAME != VCC && REF_NAME != GND}]
    set names [get_property NAME $cells]
    set types [get_property REF_NAME $cells]
    array set primitiveNames {}
    foreach name $names { set primitiveNames($name) 1 }
    set f [open [file join $out leaf_cells.tsv] w]
    puts $f "cell\tprimitive"
    set leaf_count 0
    foreach name $names type $types {
        set parent $name
        set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash - 1}]]
            if {[info exists primitiveNames($parent)]} { set internal 1; break }
        }
        if {!$internal} { puts $f "$name\t$type"; incr leaf_count }
    }
    close $f
    set clocks [get_clocks -quiet *]
    set f [open [file join $out clocks.tsv] w]
    puts $f "clock\tperiod_ns\twaveform\tis_generated\tsource_pins"
    foreach c $clocks {
        puts $f "[get_property NAME $c]\t[get_property PERIOD $c]\t[get_property WAVEFORM $c]\t[get_property IS_GENERATED $c]\t[get_property SOURCE_PINS $c]"
    }
    close $f
    report_clocks -file [file join $out clocks.rpt]
    write_xdc -type timing [file join $out original_timing.xdc]
    # Each report is best effort, while opening and exporting cells/clocks are required.
    set warnings [open [file join $out report_warnings.txt] w]
    foreach command [list \
        [list check_timing -verbose -file [file join $out check_timing.rpt]] \
        [list report_clock_interaction -file [file join $out clock_interaction.rpt]] \
        [list report_exceptions -summary -file [file join $out exceptions.rpt]] \
        [list report_utilization -file [file join $out utilization.rpt]]] {
        if {[catch {eval $command} message]} { puts $warnings "$command: $message" }
    }
    close $warnings
    set f [open [file join $out summary.tsv] w]
    puts $f "key\tvalue"
    puts $f "part\t[get_property PART [current_design]]"
    puts $f "top\t[get_property NAME [current_design]]"
    puts $f "leaf_count\t$leaf_count"
    puts $f "primitive_count\t[llength $cells]"
    puts $f "blackbox_count\t[llength [get_cells -hierarchical -quiet -filter {IS_BLACKBOX}]]"
    puts $f "port_count\t[llength [get_ports -quiet *]]"
    puts $f "clock_count\t[llength $clocks]"
    puts $f "elapsed_seconds\t[expr {([clock milliseconds]-$start)/1000.0}]"
    close $f
    close_design
    puts "AMF2_CASE_PROBE_COMPLETE"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
