# Reproduce the original netlist and site assignments, without place_design repair.
if {$argc != 2} {error "Expected input.dcp diagnostic-directory"}
lassign $argv input out
set_param general.maxThreads 4
open_checkpoint $input
puts "AMF_SITE_INIT unplace";flush stdout
place_design -unplace
foreach prop {HLUTNM SOFT_HLUTNM} {
    puts "AMF_SITE_INIT select_$prop";flush stdout
    set grouped [get_cells -hierarchical -filter "$prop != \"\""]
    puts "AMF_SITE_INIT clear_$prop count=[llength $grouped]";flush stdout
    if {[llength $grouped]} {set_property $prop {} $grouped}
}
puts "AMF_SITE_INIT finished";flush stdout
array set batches {}
set f [open [file join $out sites.tsv] r]
while {[gets $f line] >= 0} {
    lassign [split $line "\t"] site kind name target
    lappend batches($site) $name $target
}
close $f
set details [open [file join $out cell_details.tsv] w]
puts $details "site\tcell\tproperty\tvalue"
set pins [open [file join $out pins.tsv] w]
puts $pins "cell\tpin\tdirection\tnet"
set results [open [file join $out replay.tsv] w]
puts $results "site\ttrial\terror\tplaced\trequested\tmessage"
foreach site [lsort [array names batches]] {
    set pairs $batches($site)
    set names {}
    foreach {name target} $pairs {lappend names $name}
    set cells [get_cells $names]
    foreach cell $cells {
        set name [get_property NAME $cell]
        foreach prop [list_property $cell] {
            if {[regexp {^(REF_NAME|ORIG_REF_NAME|HLUTNM|SOFT_HLUTNM|BEL|LOC|INIT|IS_.*INVERTED|.*SHAPE.*|.*MACRO.*)$} $prop]} {
                puts $details "$site\t$name\t$prop\t[get_property $prop $cell]"
            }
        }
        foreach pin [get_pins -of_objects $cell] {
            puts $pins "$name\t[get_property NAME $pin]\t[get_property DIRECTION $pin]\t[get_nets -quiet -of_objects $pin]"
        }
    }
    foreach trial {original reverse individual} {
        set present [get_cells -quiet -of_objects [get_sites $site]]
        if {[llength $present]} {unplace_cell $present}
        unplace_cell $cells
        set batch $pairs
        if {$trial eq "reverse"} {
            set batch {}
            foreach {name target} $pairs {set batch [concat [list $name $target] $batch]}
        }
        set failed 0;set message ""
        if {$trial eq "individual"} {
            foreach {name target} $batch {
                if {[catch {place_cell [list $name $target]} failure]} {incr failed;append message "$failure ; "}
            }
        } else {
            set failed [catch {place_cell $batch} message]
        }
        set placed 0
        foreach cell $cells {if {[get_property LOC $cell] ne ""} {incr placed}}
        set message [string map [list "\n" " " "\t" " "] $message]
        puts $results "$site\t$trial\t$failed\t$placed\t[llength $names]\t$message"
        flush $results
        puts "AMF_SITE_REPLAY $site $trial failed=$failed placed=$placed/[llength $names]";flush stdout
    }
    set present [get_cells -quiet -of_objects [get_sites $site]]
    if {[llength $present]} {unplace_cell $present}
}
close $details;close $pins;close $results
puts "AMF_SITE_REPLAY_FINISHED"
set followup [file join $out followup.tcl]
if {[file exists $followup]} {source $followup}
exit 0
