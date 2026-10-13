lassign $argv input output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open $output w]
    puts $f "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
    set count 0
    foreach cell [lsort [get_cells -hier -filter {REF_NAME =~ SRL*}]] {
        set clk {}; set ce {}; set sinks {}
        foreach pin [get_pins -of_objects $cell] {
            set ref [get_property REF_PIN_NAME $pin]
            if {$ref eq "CLK"} {set clk [get_nets -quiet -of_objects $pin]}
            if {$ref eq "CE"} {set ce [get_nets -quiet -of_objects $pin]}
            if {$ref eq "Q31"} {
                set net [get_nets -quiet -of_objects $pin]
                if {[llength $net]} {set sinks [get_pins -quiet -of_objects $net -filter {DIRECTION == IN}]}
            }
        }
        set invert UNKNOWN
        if {[lsearch -exact [list_property $cell] IS_CLK_INVERTED] >= 0} {
            set invert [get_property IS_CLK_INVERTED $cell]
        }
        puts $f [join [list $cell [get_property REF_NAME $cell] $clk $ce $invert [get_property IS_LOC_FIXED $cell] [get_property IS_BEL_FIXED $cell] $sinks] "\t"]
        incr count
    }
    close $f
    puts "SRL_CONTROL_EXTRACTION_PASS cells=$count version=[version -short]"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 2
}
exit 0
