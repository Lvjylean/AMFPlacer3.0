source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_srl_metadata.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    set begin [clock milliseconds]
    open_checkpoint $out/base_opt.dcp
    if {![string match 2026.1* [version -short]]} {error "Unexpected Vivado version"}
    set clocks [get_clocks -quiet $dfx(clock_name)]
    if {[llength $clocks] != 1 || abs([get_property PERIOD $clocks] - $dfx(clock_period_ns)) > 0.000001} {error "Reused clock mismatch"}
    set all [get_cells -quiet -hierarchical -filter {REF_NAME == SRL16E || REF_NAME == SRLC32E}]
    set names [get_property NAME $all]; set types [get_property REF_NAME $all]
    array set objects {}; array set kind {}; array set seen {}
    foreach obj $all name $names type $types {
        set objects($name) $obj; set kind($name) $type
    }
    set input [open $out/srl_controls.before.tsv r]
    set output [open $out/srl_controls.tsv w]
    gets $input header; puts $output $header
    set audit [open $out/srl_inversion_audit.tsv w]
    puts $audit "cell\ttype\tproperty_available\traw_inversion\tprevious_inversion"
    set count 0; set changed 0; set empty 0; set unknown 0
    while {[gets $input line] >= 0} {
        set row [split $line \t]
        if {[llength $row] != 8} {error "Malformed original SRL metadata"}
        lassign $row name type clk ce old loc bel sinks
        if {![info exists objects($name)] || [info exists seen($name)] || $kind($name) ne $type} {error "SRL name/type mismatch: $name"}
        set actualLoc [get_property IS_LOC_FIXED $objects($name)]
        set actualBel [get_property IS_BEL_FIXED $objects($name)]
        foreach value [list $actualLoc $actualBel $loc $bel] {
            if {![string is boolean -strict $value]} {error "Invalid SRL fixed boolean: $name value=<$value>"}
        }
        if {bool($actualLoc) != bool($loc) || bool($actualBel) != bool($bel)} {
            error "SRL original fixed flags changed: $name original_loc=<$loc> actual_loc=<$actualLoc> original_bel=<$bel> actual_bel=<$actualBel>"
        }
        set seen($name) 1
        set inv [amf_dfx_srl::inversion $objects($name) $type]
        if {$inv ne $old} {incr changed}
        if {$inv eq ""} {incr empty}
        if {$inv eq "UNKNOWN"} {incr unknown}
        lset row 4 $inv
        puts $output [join $row \t]
        puts $audit [join [list $name $type $amf_dfx_srl::inversionAvailable($type) $inv $old] \t]
        incr count
    }
    close $input; close $output; close $audit
    if {$count != [array size objects]} {error "Incomplete SRL coverage"}
    set f [open $out/srl_refresh_summary.tsv w]
    foreach {k v} [list rows $count changed_rows $changed raw_empty_rows $empty unknown_rows $unknown] {puts $f "$k\t$v"}
    close $f
    set seconds [expr {([clock milliseconds] - $begin)/1000.0}]
    set f [open $out/stage_times.tsv a]; puts $f "srl_metadata_refresh\t$seconds\t0"; close $f
    puts "AMF_SRL_METADATA_REFRESH rows=$count changed=$changed raw_empty=$empty unknown=$unknown seconds=$seconds"
    close_design
}
if {[catch {main} message options]} {puts stderr $message; puts stderr [dict get $options -errorinfo]; exit 2}
exit 0
