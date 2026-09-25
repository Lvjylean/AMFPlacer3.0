# Read-only verification of final cell locations against AMF requests.
set_param general.maxThreads 4
if {[catch {
    set dcp [lindex $argv 0]
    set requests [lindex $argv 1]
    set output [lindex $argv 2]
    set fixedFile [lindex $argv 3]
    set targets [dict create]
    set input [open $requests r]
    while {[gets $input line] >= 0} {
        lassign [split $line "\t"] name target
        dict set targets $name $target
    }
    close $input
    open_checkpoint $dcp
    set found 0
    set exact 0
    set changed 0
    set unplaced 0
    set changedFile [open "${output}/placement_changes.tsv" w]
    puts $changedFile "cell\tamf_target\tfinal_target"
    foreach cell [xilinx::designutils::get_leaf_cells *] {
        if {![dict exists $targets $cell]} {continue}
        set requested [dict get $targets $cell]
        set location [get_property LOC $cell]
        incr found
        if {$location eq ""} {
            incr unplaced
            puts $changedFile "$cell\t$requested\tUNPLACED"
            continue
        }
        set actual $location
        if {[string first "/" $requested] >= 0} {
            append actual "/" [lindex [split [get_property BEL $cell] "."] end]
        }
        if {$actual eq $requested} {
            incr exact
        } else {
            incr changed
            puts $changedFile "$cell\t$requested\t$actual"
        }
    }
    close $changedFile
    set fixedCount 0
    set fixedMatches 0
    if {$fixedFile ne ""} {
        set f [open $fixedFile r]
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] name target
            incr fixedCount
            set cell [get_cells -quiet $name]
            if {[llength $cell] != 1} {error "Fixed cell missing: $name"}
            set actual "[get_property LOC $cell]/[lindex [split [get_property BEL $cell] "."] end]"
            if {$actual eq $target} {incr fixedMatches}
        }
        close $f
    }
    set result [open "${output}/placement_audit.json" w]
    puts $result "\{\"requested_cells\":[dict size $targets],\"found_cells\":$found,\"exact_location_matches\":$exact,\"changed_locations\":$changed,\"unplaced_requested_cells\":$unplaced,\"input_fixed_cells\":$fixedCount,\"fixed_location_matches\":$fixedMatches\}"
    close $result
    puts "PLACEMENT_AUDIT requested=[dict size $targets] found=$found exact=$exact changed=$changed unplaced=$unplaced"
    close_design
    if {$found != [dict size $targets] || $unplaced != 0} {error "Final DCP has missing or unplaced requested cells"}
    if {$fixedMatches != $fixedCount} {error "Final DCP moved input fixed cells"}
} msg opts]} {
    puts "PLACEMENT_AUDIT_FAILED=$msg"
    puts [dict get $opts -errorinfo]
    exit 1
}
exit 0
