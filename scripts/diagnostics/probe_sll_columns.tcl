# Read-only U250 SLL object inspection. Does not place, route, or write a DCP.
if {$argc != 2} { error "usage: probe_sll_columns.tcl routed.dcp output_dir" }
set checkpoint [lindex $argv 0]
set output_dir [lindex $argv 1]
file mkdir $output_dir
set_param general.maxThreads 4
help get_nodes
open_checkpoint $checkpoint
set f [open [file join $output_dir probe.txt] w]
puts $f "VIVADO [version -short]"
puts $f "PART [get_property PART [current_design]]"
foreach slr [get_slrs] {
    puts $f "SLR $slr"
    foreach prop [list_property $slr] {
        if {[string match *SLL* $prop] || [string match *INDEX* $prop]} {
            puts $f "$prop [get_property $prop $slr]"
        }
    }
}
foreach pair {{SLR0 SLR1} {SLR1 SLR2} {SLR2 SLR3}} {
    set nodes [get_nodes -slls_between [get_slrs $pair]]
    puts $f "PAIR $pair COUNT [llength $nodes]"
    set node [lindex $nodes 0]
    puts $f "SAMPLE $node"
    foreach prop [list_property $node] {
        puts $f "NODE_PROPERTY $prop [get_property $prop $node]"
    }
    foreach tile [get_tiles -of_objects $node] {
        puts $f "TILE $tile SLR [get_slrs -of_objects $tile]"
        foreach prop [list_property $tile] {
            puts $f "TILE_PROPERTY $prop [get_property $prop $tile]"
        }
    }
    if {[lsearch -exact [list_property $node] IS_USED] >= 0} {
        set used [filter $nodes {IS_USED == 1}]
        puts $f "USED [llength $used]"
        foreach n [lrange $used 0 2] {
            puts $f "USED_NODE $n NET [get_nets -of_objects $n] TILES [get_tiles -of_objects $n]"
        }
    }
    flush $f
}
close $f
puts "SLL_PROBE_COMPLETE"
close_design
