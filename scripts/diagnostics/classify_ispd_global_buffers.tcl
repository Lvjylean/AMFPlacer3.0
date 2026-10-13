# Classify by actual sink pins, not by BUFG primitive or port name alone.
# IS_CLOCK identifies clock pins including Vivado's decomposed DSP primitives.
proc ispd_clock_buffers {report_path} {
    set out [open $report_path w]
    puts $out "buffer\tsink_count\tclock_sink_count\tsink_pin_types"
    set clock_buffers {}
    foreach buffer [get_cells -hier -filter {REF_NAME == BUFGCE}] {
        set nets [get_nets -segments [get_nets -of_objects [get_pins $buffer/O]]]
        set sinks [get_pins -quiet -leaf -of_objects $nets -filter {DIRECTION == IN}]
        set clock_count 0
        set types [dict create]
        foreach pin $sinks refpin [get_property REF_PIN_NAME $sinks] is_clock [get_property IS_CLOCK $sinks] {
            set cell [get_cells -of_objects $pin]
            set type [get_property REF_NAME $cell]
            dict incr types "$type:$refpin"
            if {$is_clock ni {0 1}} {error "Clock-pin classification unavailable for $pin"}
            if {$is_clock} {incr clock_count}
        }
        puts $out "$buffer\t[llength $sinks]\t$clock_count\t$types"
        if {$clock_count > 0} {lappend clock_buffers $buffer}
    }
    close $out
    return $clock_buffers
}
