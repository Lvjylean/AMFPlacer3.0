# Audit whether legacy control-net identities hide a polarity conflict in this case.
if {$argc != 2} {error "Expected checkpoint new-output-directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite polarity evidence"}
file mkdir $output
source [file join [file dirname [file dirname [info script]]] full_design_interfaces.tcl]
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open [file join $output ff_polarities.tsv] w]
    puts $f "cell\tprimitive\tpin\tinverted"
    set counts [dict create]
    foreach {kind srPin} {FDRE R FDSE S FDCE CLR FDPE PRE} {
        set cells [get_cells -hier -filter "REF_NAME == $kind && IS_PRIMITIVE"]
        dict set counts $kind [llength $cells]
        set pins [get_pins -of_objects $cells -filter "IS_INVERTED && (REF_PIN_NAME == C || REF_PIN_NAME == $srPin)"]
        foreach pin [amf_full::properties NAME $pins] {
            set split [string last / $pin]
            puts $f "[string range $pin 0 [expr {$split-1}]]\t$kind\t[string range $pin [expr {$split+1}] end]\t1"
        }
    }
    close $f
    set f [open [file join $output ff_counts.tsv] w]
    dict for {kind count} $counts {puts $f "$kind\t$count"}
    close $f
    puts "FF_POLARITY_AUDIT $counts"
} message options]} {
    puts stderr $message;puts stderr [dict get $options -errorinfo];exit 1
}
exit 0
