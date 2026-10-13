lassign $argv input packing out
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open $packing r];gets $f header
    array set macros {}
    while {[gets $f line] >= 0} {
        lassign [split $line "\t"] macro name type bel
        lappend macros($macro) $name $bel
    }
    close $f
    set sites [lsort [get_sites -filter {SITE_TYPE == SLICEM}]]
    set idx 0;set count 0;set targets {}
    foreach macro [lsort [array names macros]] {
        set site [lindex $sites $idx];incr idx
        foreach {name bel} $macros($macro) {lappend targets $name $site/$bel;incr count}
    }
    place_cell $targets
    foreach {name target} $targets {
        set cell [get_cells -quiet $name]
        set actual "[get_property LOC $cell]/[lindex [split [get_property BEL $cell] .] end]"
        if {$actual ne $target} {error "Packing mismatch: $name $actual $target"}
    }
    set f [open [file join $out validation.json] w]
    puts $f [format {{"macros":%d,"cells":%d,"exact_matches":%d,"vivado_version":"%s","full_placement_executed":false}} $idx $count $count [version -short]]
    close $f
    puts "AMF_SRL_PACKING_AUDIT_OK macros=$idx cells=$count"
} failure options]} {
    puts stderr "AMF_SRL_PACKING_AUDIT_FAILED: $failure"
    puts stderr [dict get $options -errorinfo]
    exit 2
}
exit 0
