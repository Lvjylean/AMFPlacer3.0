# In-memory diagnostic only: no checkpoint or constraint is written.
lassign $argv dcp names output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $dcp
    set f [open $names r]
    array set wanted {}
    while {[gets $f line]>=0} {
        lassign [split $line "\t"] parsed original
        set wanted($original) $parsed
    }
    close $f
    set f [open $output w]
    puts $f "input_name\tparsed_name\tinput_query_matches\tparsed_query_matches\traw_place_exit\tlist_place_exit\traw_place_message"
    set cells [get_cells -hier -filter {IS_PRIMITIVE}]
    set found 0
    foreach cell $cells name [get_property NAME $cells] {
        if {![info exists wanted($name)]} {continue}
        incr found
        set parsed $wanted($name)
        set originalMatches [get_property NAME [get_cells -quiet $name]]
        set parsedMatches [get_property NAME [get_cells -quiet $parsed]]
        set loc [get_property LOC $cell]
        set bel [lindex [split [file tail [get_property BEL $cell]] .] end]
        if {$loc eq "" || $bel eq ""} {error "Expected a placed source cell: $name"}
        set target "$loc/$bel"
        # Mimic the original export's unquoted name inside the placement list.
        set rawCode [catch {place_cell "$name $target"} rawMessage]
        set listCode [catch {place_cell [list $name $target]} listMessage]
        puts $f "$name\t$parsed\t$originalMatches\t$parsedMatches\t$rawCode\t$listCode\t[string map [list \n { } \t { }] $rawMessage]"
        flush $f
        puts "NAME_PROBE $found raw=$rawCode list=$listCode";flush stdout
    }
    close $f
    if {$found != [array size wanted]} {error "Missing input names: found=$found expected=[array size wanted]"}
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
