# Preserve synthesis/IP ownership independently of physical fixedness.
proc amf_full::export_attributes {output} {
    set f [open [file join $output protected_cells.tsv] w]
    puts $f "cell\tprimitive\tdont_touch\tasync_reg\tloc_fixed\tbel_fixed"
    foreach c [get_cells -hier -filter {DONT_TOUCH == TRUE || ASYNC_REG == TRUE}] {
        puts $f [join [list $c [get_property REF_NAME $c] [amf_full::bool_prop $c DONT_TOUCH] [amf_full::bool_prop $c ASYNC_REG] [amf_full::bool_prop $c IS_LOC_FIXED] [amf_full::bool_prop $c IS_BEL_FIXED]] "\t"]
    }
    close $f
    set f [open [file join $output async_regs.tsv] w]
    puts $f "cell\tprimitive\tclock_driver\tclock_inverted\tce_driver\tsr_driver\tcontrol_polarities\td_driver\tdont_touch"
    foreach c [get_cells -hier -filter {ASYNC_REG == TRUE && IS_PRIMITIVE}] {
        set kind [get_property REF_NAME $c]
        if {$kind ni {FDRE FDSE FDCE FDPE}} {error "Unsupported ASYNC_REG primitive: $c $kind"}
        set cp [get_pins -of_objects $c -filter {REF_PIN_NAME == C}]
        set ce [get_pins -of_objects $c -filter {REF_PIN_NAME == CE}]
        set sr [get_pins -of_objects $c -filter {REF_PIN_NAME == R || REF_PIN_NAME == S || REF_PIN_NAME == CLR || REF_PIN_NAME == PRE}]
        set dp [get_pins -of_objects $c -filter {REF_PIN_NAME == D}]
        set polarities {}
        foreach pin [concat $ce $sr] {lappend polarities "[get_property REF_PIN_NAME $pin]=[get_property IS_INVERTED $pin]"}
        puts $f [join [list $c $kind [amf_full::driver $cp] [get_property IS_INVERTED $cp] [amf_full::driver $ce] [amf_full::driver $sr] [join $polarities ,] [amf_full::driver $dp] [amf_full::bool_prop $c DONT_TOUCH]] "\t"]
    }
    close $f
    set f [open [file join $output srl_controls.tsv] w]
    puts $f "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
    foreach cell [get_cells -hier -filter {REF_NAME == SRL16E || REF_NAME == SRLC32E}] {
        set clk {};set ce {};set sinks {}
        foreach pin [get_pins -of_objects $cell] {
            set ref [get_property REF_PIN_NAME $pin]
            if {$ref eq "CLK"} {set clk [get_nets -quiet -of_objects $pin]}
            if {$ref eq "CE"} {set ce [get_nets -quiet -of_objects $pin]}
            if {$ref eq "Q31"} {
                set net [get_nets -quiet -segments -of_objects $pin]
                if {[llength $net]} {set sinks [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]}
            }
        }
        set invert UNKNOWN
        if {[lsearch -exact [list_property $cell] IS_CLK_INVERTED] >= 0} {set invert [get_property IS_CLK_INVERTED $cell]}
        puts $f [join [list $cell [get_property REF_NAME $cell] $clk $ce $invert [get_property IS_LOC_FIXED $cell] [get_property IS_BEL_FIXED $cell] $sinks] "\t"]
    }
    close $f
    set f [open [file join $output clock_route_properties.tsv] w]
    puts $f "object\tclass\tvalue"
    foreach cls {net pin} {
        if {$cls eq "net"} {set objects [get_nets -hier -quiet -filter {CLOCK_DEDICATED_ROUTE != "" && CLOCK_DEDICATED_ROUTE != TRUE}]} else {
            set objects [get_pins -hier -quiet -filter {CLOCK_DEDICATED_ROUTE != "" && CLOCK_DEDICATED_ROUTE != TRUE}]
        }
        foreach obj $objects {puts $f "$obj\t$cls\t[get_property CLOCK_DEDICATED_ROUTE $obj]"}
    }
    close $f
}
