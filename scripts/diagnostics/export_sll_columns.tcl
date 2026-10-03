# Read-only U250 routed SLL resource inventory, verified with Vivado 2024.2.
# Physical columns use tile COLUMN, not the X suffix in a tile/site name.
if {$argc != 2} { error "usage: export_sll_columns.tcl routed.dcp output_dir" }
set checkpoint [lindex $argv 0]
set output_dir [lindex $argv 1]
file mkdir $output_dir
set_param general.maxThreads 4
open_checkpoint $checkpoint
if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {
    error "This diagnostic currently validates only xcu250-figd2104-2L-e"
}
set raw [open [file join $output_dir sll_nodes.tsv] w]
puts $raw "boundary\tcolumn\tnode\tused\tbad\tendpoint_tiles\tnets"
set summary [open [file join $output_dir sll_columns.tsv] w]
puts $summary "boundary\tcolumn\tphysical_capacity\trouted_used\tbad_nodes\trouted_utilization"
set totals [open [file join $output_dir sll_boundaries.tsv] w]
puts $totals "boundary\tphysical_capacity\trouted_used\tcolumn_count"
foreach pair {{SLR0 SLR1} {SLR1 SLR2} {SLR2 SLR3}} {
    set boundary [join $pair :]
    set nodes [get_nodes -slls_between [get_slrs $pair]]
    set occupied [get_nodes -slls_between [get_slrs $pair] -filter {IS_USED == 1}]
    set used_names [dict create]
    foreach used_node $occupied {
        dict set used_names [get_property NAME $used_node] 1
    }
    set capacity [dict create]
    set usage [dict create]
    set bad_counts [dict create]
    set seen [dict create]
    foreach node $nodes {
        set name [get_property NAME $node]
        if {[dict exists $seen $name]} { error "Duplicate SLL node $name" }
        dict set seen $name 1
        if {![get_property IS_SLL $node]} { error "Non-SLL node returned: $name" }
        set tiles [get_tiles -of_objects $node]
        set columns [lsort -unique [get_property COLUMN $tiles]]
        if {[llength $columns] != 1} { error "Unaligned SLL endpoints: $name $columns" }
        set column [lindex $columns 0]
        set used [dict exists $used_names $name]
        set bad [get_property IS_BAD $node]
        dict incr capacity $column
        dict incr usage $column $used
        dict incr bad_counts $column $bad
        set nets {}
        if {$used} {
            set nets [get_nets -quiet -of_objects $node]
            if {[llength $nets] == 0} { error "Occupied SLL has no net: $name" }
        }
        puts $raw [join [list $boundary $column $name $used $bad [join $tiles ,] [join $nets ,]] "\t"]
    }
    foreach column [lsort -integer [dict keys $capacity]] {
        set count [dict get $capacity $column]
        set used [dict get $usage $column]
        puts $summary [join [list $boundary $column $count $used [dict get $bad_counts $column] [format %.9f [expr {double($used)/$count}]]] "\t"]
    }
    puts $totals [join [list $boundary [llength $nodes] [llength $occupied] [dict size $capacity]] "\t"]
    flush $raw
    flush $summary
    flush $totals
    puts "SLL_BOUNDARY_COMPLETE $boundary capacity=[llength $nodes] used=[llength $occupied]"
}
close $raw
close $summary
close $totals
report_utilization -slr -file [file join $output_dir utilization.rpt]
puts "SLL_EXPORT_COMPLETE"
close_design
