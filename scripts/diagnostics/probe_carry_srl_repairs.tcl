# Source after replay_import_sites.tcl, while its original DCP is still open.
set fixes [open [file join $out repair_probes.tsv] w]
puts $fixes "site\ttrial\terror\tplaced\trequested\tmessage"
proc probe_batch {site trial pairs} {
    global fixes
    set names {}
    foreach {name target} $pairs {lappend names $name}
    set cells [get_cells $names]
    unplace_cell $cells
    set error [catch {place_cell $pairs} message]
    set placed 0
    foreach cell $cells {if {[get_property LOC $cell] ne ""} {incr placed}}
    puts $fixes "$site\t$trial\t$error\t$placed\t[llength $names]\t[string map [list "\n" " " "\t" " "] $message]"
    flush $fixes
    unplace_cell $cells
}
foreach site {SLICE_X101Y199 SLICE_X101Y301 SLICE_X101Y541} {
    foreach mode {remove_cff remove_bottom_ffs move_bottom_ffs} {
        set batch {};set index 0
        foreach {name target} $batches($site) {
            set bel [lindex [split $target /] 1]
            if {$mode eq "remove_cff" && $bel eq "CFF"} {continue}
            if {[regexp {^[A-D]FF2?$} $bel]} {
                if {$mode eq "remove_bottom_ffs"} {continue}
                if {$mode eq "move_bottom_ffs"} {
                    set target "SLICE_X101Y198/[format %c [expr {65+$index/2}]]FF"
                    if {$index%2} {append target 2}
                    incr index
                }
            }
            lappend batch $name $target
        }
        probe_batch $site $mode $batch
    }
}
set site SLICE_X101Y199
set base {};set ff ""
foreach {name target} $batches($site) {
    if {[regexp {/([A-H]FF2?)$} $target]} {if {[string match */CFF $target]} {set ff $name};continue}
    lappend base $name $target
}
foreach bel {AFF AFF2 BFF BFF2 CFF CFF2 DFF DFF2 EFF EFF2 FFF FFF2 GFF GFF2 HFF HFF2} {
    probe_batch $site "single_carry_ff_$bel" [concat $base [list $ff $site/$bel]]
}
foreach site {SLICE_X114Y230 SLICE_X116Y361 SLICE_X125Y131} {
    foreach mode {remove_aff move_aff_to_hff} {
        set batch {}
        foreach {name target} $batches($site) {
            if {[string match */AFF $target]} {
                if {$mode eq "remove_aff"} {continue}
                set target "$site/HFF"
            }
            lappend batch $name $target
        }
        probe_batch $site $mode $batch
    }
}
set site SLICE_X81Y91
set tail [lindex $batches($site) 0]
set chain [list $tail]
set current [get_cells $tail]
while {1} {
    set source [get_pins -quiet -leaf -of_objects [get_nets -quiet -of_objects [get_pins $current/D]] -filter {DIRECTION == OUT}]
    if {[llength $source] != 1 || ![string match */Q31 [get_property NAME $source]]} {break}
    set current [get_cells -of_objects $source]
    if {![string match SRL* [get_property REF_NAME $current]]} {break}
    lappend chain [get_property NAME $current]
}
foreach start {0 3 5} {
    set batch {};set index $start
    foreach name $chain {lappend batch $name "$site/[format %c [expr {65+$index}]]6LUT";incr index}
    probe_batch $site "srl_chain_tail_$start" $batch
}
close $fixes
puts "AMF_REPAIR_PROBES_FINISHED"
