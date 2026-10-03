# Prepare the clock and fixed I/O boundary for a U250 part-level experiment.
# Package pins are selected by Vivado; this does not implement a board interface.
if {$argc != 3} {error "Expected unplaced.dcp, new output directory, period_ns"}
lassign $argv input output period
source [file join [file dirname [info script]] classify_ispd_global_buffers.tcl]
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
set timeline [open [file join $output stages.tsv] w]
puts $timeline "stage\tseconds"
proc timed {name body} {
    global timeline
    puts "ISPD_INPUT_STAGE_START $name";flush stdout
    set start [clock milliseconds]
    uplevel 1 $body
    puts $timeline "$name\t[expr {([clock milliseconds]-$start)/1000.0}]";flush $timeline
    puts "ISPD_INPUT_STAGE_DONE $name";flush stdout
}
if {[catch {
    timed open {open_checkpoint $input}
    if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {error "Unexpected target part"}
    if {[llength [get_clocks -quiet *]]} {error "Expected unconstrained migrated ISPD design"}
    set buffers [get_cells -hier -filter {REF_NAME == BUFGCE}]
    set true_clock_buffers [ispd_clock_buffers [file join $output global_buffer_sinks.tsv]]
    set clock_ports {}
    set bindings [open [file join $output clock_sources.tsv] w]
    puts $bindings "buffer\tstartpoints\tclasses\tdrives_clock_pins"
    foreach buffer $buffers {
        set roots [all_fanin -flat -startpoints_only -to [get_pins $buffer/I]]
        puts $bindings "$buffer\t[join $roots ,]\t[join [get_property CLASS $roots] ,]\t[expr {$buffer in $true_clock_buffers}]"
        puts "ISPD_CLOCK_SOURCE $buffer : $roots"
        if {$buffer ni $true_clock_buffers} {continue}
        foreach root $roots {
            if {[get_property CLASS $root] ne "port"} {error "Non-port clock source requires explicit handling: $buffer / $root"}
            lappend clock_ports $root
        }
        if {![llength $roots]} {error "Clock buffer has no source: $buffer"}
    }
    close $bindings
    set clock_ports [lsort -unique $clock_ports]
    timed clocks {
        foreach port $clock_ports {
            create_clock -name [get_property NAME $port] -period $period $port
        }
        # No asynchronous groups or timing exceptions are inferred from a netlist.
        foreach buffer $true_clock_buffers {
            if {[llength [get_clocks -quiet -of_objects [get_pins $buffer/O]]] != 1} {
                error "Expected one clock propagating through $buffer/O"
            }
        }
        report_clocks -file [file join $output clocks.rpt]
        write_xdc -type timing [file join $output timing.xdc]
    }
    timed io_placement {place_ports}
    set fixed [get_cells -hier -filter {REF_NAME == IBUF || REF_NAME == OBUF || REF_NAME == BUFGCE}]
    set f [open [file join $output fixed_candidates.tsv] w]
    puts $f "cell\tprimitive\tsite\tbel\tfixed"
    set missing {}
    foreach name [get_property NAME $fixed] type [get_property REF_NAME $fixed] site [get_property LOC $fixed] bel [get_property BEL $fixed] locked [get_property IS_LOC_FIXED $fixed] {
        puts $f "$name\t$type\t$site\t$bel\t$locked"
        if {$site eq "" || $bel eq ""} {lappend missing $name}
    }
    close $f
    puts "ISPD_FIXED_RESOURCE_COUNT [llength $fixed] MISSING [llength $missing]"
    if {[llength $missing]} {
        puts "ISPD_MISSING_FIXED_RESOURCES $missing"
        puts [help place_design]
        puts [help unplace_cell]
        error "Clock or I/O cells need a legal fixed assignment before AMF export"
    }
    timed checkpoint {write_checkpoint [file join $output clock_io_prepared.dcp]}
    puts "ISPD_CLOCK_IO_PREPARED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    close $timeline
    exit 1
}
close $timeline
exit 0
