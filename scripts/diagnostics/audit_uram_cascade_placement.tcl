# Apply only AMF's URAM assignments to a fresh in-memory copy of the frozen DCP.
if {$argc != 3} {error "Expected checkpoint resource-directory new-output-directory"}
lassign $argv checkpoint resources output
if {[file exists $output]} {error "Refusing to overwrite audit"}
file mkdir $output
source [file join [file dirname [file dirname [info script]]] full_interface_contract.tcl]
set_param general.maxThreads 4
proc fixed_signature {cells} {
    set rows {}
    foreach c $cells {
        lappend rows [list $c [get_property LOC $c] [get_property BEL $c] [get_property IS_LOC_FIXED $c] [get_property IS_BEL_FIXED $c]]
    }
    return [lsort $rows]
}
if {[catch {
    open_checkpoint $checkpoint
    set protected [amf_contract::signature]
    set originalFixed [get_cells -hier -filter {IS_LOC_FIXED || IS_BEL_FIXED}]
    set fixed [fixed_signature $originalFixed]
    set assignments {};set requested [dict create]
    set f [open [file join $resources resources.tsv] r];gets $f header
    while {[gets $f line]>=0} {
        lassign [split $line "\t"] name kind site bel slr
        if {$kind ni {URAM288 URAM288_BASE}} {continue}
        if {[dict exists $requested $name]} {error "Duplicate URAM assignment"}
        dict set requested $name [list $site $bel $slr]
        lappend assignments $name $site/$bel
    }
    close $f
    if {[dict size $requested] != [llength [get_cells -hier -filter {REF_NAME == URAM288 || REF_NAME == URAM288_BASE}]]} {
        error "Incomplete URAM coverage"
    }
    place_cell $assignments
    set f [open [file join $output uram-placement.tsv] w]
    puts $f "cell\tsite\tbel\tslr"
    dict for {name values} $requested {
        lassign $values site bel slr
        set c [get_cells -quiet $name]
        if {[llength $c]!=1 || [get_property LOC $c] ne $site ||
            [lindex [split [get_property BEL $c] .] end] ne $bel ||
            [get_slrs -of_objects [get_sites $site]] ne "SLR$slr"} {error "URAM assignment mismatch: $name"}
        puts $f "$name\t$site\t$bel\t$slr"
    }
    close $f
    set edges 0
    set f [open [file join $resources cascades.tsv] r];gets $f header
    while {[gets $f line]>=0} {
        lassign [split $line "\t"] a b family
        if {$family ne "URAM288"} {continue}
        lassign [dict get $requested $a] as ab al
        lassign [dict get $requested $b] bs bb bl
        if {![regexp {URAM288_X([0-9]+)Y([0-9]+)} $as -> ax ay] || ![regexp {URAM288_X([0-9]+)Y([0-9]+)} $bs -> bx by]} {error "Unknown URAM site"}
        if {$ax!=$bx || $by!=$ay+1 || $al!=$bl} {error "Invalid URAM cascade adjacency: $a -> $b"}
        incr edges
    }
    close $f
    # place_cell fixes newly placed URAMs. Check the original protected owners,
    # rather than treating the intended new assignments as interface changes.
    set fixedAfter [fixed_signature $originalFixed]
    set protectedAfter [amf_contract::signature]
    foreach {name rows} [list fixed-before $fixed fixed-after $fixedAfter protected-before $protected protected-after $protectedAfter] {
        set f [open [file join $output $name.txt] w];puts $f [join $rows "\n"];close $f
    }
    if {$fixed ne $fixedAfter || $protected ne $protectedAfter} {error "Fixed interfaces or protection/clock/port contract changed"}
    set f [open [file join $output result.json] w]
    puts $f [format {{"uram_cells":%d,"cascade_edges":%d,"fixed_records":%d,"protected_clock_port_records":%d,"contract_unchanged":true,"routing_executed":false,"dcp_written":false}} [dict size $requested] $edges [llength $fixed] [llength $protected]]
    close $f
    puts "URAM_CASCADE_REPLAY_OK cells=[dict size $requested] edges=$edges"
} message options]} {
    puts stderr $message;puts stderr [dict get $options -errorinfo];exit 1
}
exit 0
