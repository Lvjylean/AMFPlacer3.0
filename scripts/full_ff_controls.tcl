# Query effective pin inversion, not optional cell IS_*_INVERTED properties.
# Bulk queries avoid a per-register Vivado collection lookup on large designs.
proc amf_full::export_ff_controls {output} {
    set f [open [file join $output ff_control_inversions.tsv] w]
    puts $f "cell\tprimitive\tpin\tinverted"
    set counts [open [file join $output ff_control_counts.tsv] w]
    puts $counts "primitive\tcount"
    set total 0
    foreach {kind clockPin srPin cePin} {FDRE C R CE FDSE C S CE FDCE C CLR CE FDPE C PRE CE LDCE G CLR GE AND2B1L {} SRI {}} {
        set cells [get_cells -hier -filter "REF_NAME == $kind && IS_PRIMITIVE"]
        puts $counts "$kind\t[llength $cells]"
        incr total [llength $cells]
        if {![llength $cells]} {continue}
        set filters {}
        foreach pin [list $clockPin $srPin $cePin] {if {$pin ne ""} {lappend filters "REF_PIN_NAME == $pin"}}
        set pins [get_pins -of_objects $cells -filter "IS_INVERTED && ([join $filters { || }])"]
        foreach pin [amf_full::properties NAME $pins] {
            set split [string last / $pin]
            set ref [string range $pin [expr {$split+1}] end]
            if {$ref eq $cePin} {error "Unsupported inverted clock enable: $pin"}
            puts $f "[string range $pin 0 [expr {$split-1}]]\t$kind\t$ref\t1"
        }
    }
    close $f
    close $counts
    puts "FF_CONTROL_POLARITIES_EXPORTED $total"
}
