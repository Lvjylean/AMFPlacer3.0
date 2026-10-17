# Keep Vivado's raw SRL inversion value. Empty and absent are different.
namespace eval amf_dfx_srl {
    variable inversionAvailable; array set inversionAvailable {}
    proc inversion {cell primitive} {
        variable inversionAvailable
        if {$primitive ni {SRL16E SRLC32E}} {error "Not a supported SRL primitive: $primitive"}
        if {![info exists inversionAvailable($primitive)]} {
            set inversionAvailable($primitive) [expr {[lsearch -exact [list_property $cell] IS_CLK_INVERTED] >= 0}]
        }
        if {!$inversionAvailable($primitive)} {return UNKNOWN}
        return [get_property IS_CLK_INVERTED $cell]
    }
}
