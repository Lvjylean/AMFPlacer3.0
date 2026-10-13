# Read original contest checkpoints without modifying them. All exports go to a new directory.
if {$argc < 2} {error "Expected output directory and one or more original DCP paths"}
set output [file normalize [lindex $argv 0]]
set_param general.maxThreads 4
if {[catch {
    foreach input [lrange $argv 1 end] {
        set case_name [file tail [file dirname $input]]
        set directory [file join $output $case_name]
        if {[file exists $directory]} {error "Refusing to overwrite $directory"}
        file mkdir $directory
        set start [clock milliseconds]
        open_checkpoint $input
        set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
        set clocks [get_clocks -quiet *]
        set f [open [file join $directory summary.tsv] w]
        puts $f "key\tvalue"
        puts $f "part\t[get_property PART [current_design]]"
        puts $f "top\t[get_property NAME [current_design]]"
        puts $f "primitive_count\t[llength $cells]"
        puts $f "port_count\t[llength [get_ports -quiet *]]"
        puts $f "clock_count\t[llength $clocks]"
        puts $f "open_and_query_seconds\t[expr {([clock milliseconds]-$start)/1000.0}]"
        close $f
        report_clocks -file [file join $directory clocks.rpt]
        write_xdc -type timing [file join $directory original_timing.xdc]
        set f [open [file join $directory primitives.tsv] w]
        puts $f "cell\tprimitive"
        foreach name [get_property NAME $cells] kind [get_property REF_NAME $cells] {
            puts $f "$name\t$kind"
        }
        close $f
        puts "ISPD_CONSTRAINT_PROBE $case_name clocks=[llength $clocks]"
        close_design
    }
    help write_edif
    help link_design
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
