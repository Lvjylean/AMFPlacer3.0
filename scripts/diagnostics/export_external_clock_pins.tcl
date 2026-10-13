# Export all input pins on each external clock port's hierarchical net segments.
# Python filters hierarchy/Unisim internals using the canonical logical cell list.
if {[llength $argv] != 2} {error "Expected input DCP and new output TSV"}
set input [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
if {[file exists $output]} {error "Refusing to overwrite $output"}
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open $output w]
    puts $f "dcp_sha256\t[lindex [exec sha256sum -- $input] 0]"
    puts $f "driver_pin\tpin_name"
    set count 0
    foreach port [get_ports -quiet -filter {DIRECTION == IN}] {
        set clocks [get_clocks -quiet -of_objects $port]
        if {[llength $clocks] == 0} {continue}
        set driver "@PORT/[get_property NAME $port]"
        set nets [get_nets -segments -of_objects $port]
        set pins [get_pins -quiet -of_objects $nets -filter {DIRECTION == IN}]
        foreach name [get_property NAME $pins] {puts $f "$driver\t$name"; incr count}
        puts "AMF_EXTERNAL_CLOCK $driver [llength $pins]"
    }
    close $f
    puts "AMF_EXTERNAL_CLOCK_EXPORT_OK=$count"
    close_design
} msg opts]} {
    puts stderr $msg
    puts stderr [dict get $opts -errorinfo]
    exit 1
}
exit 0
