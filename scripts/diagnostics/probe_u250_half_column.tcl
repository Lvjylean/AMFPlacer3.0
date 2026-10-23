# Read-only investigation of the device clock connectivity; no placement.
# Usage: vivado ... -source this.tcl -tclargs part new-output-dir
set_param general.maxThreads 4
if {[llength $argv] != 2} {error "Expected part and new output directory"}
set part [lindex $argv 0]
set out [file normalize [lindex $argv 1]]
if {[file exists $out]} {error "Output already exists: $out"}
file mkdir $out
set evidence [open $out/queries.txt w]
proc query {label command} {
    global evidence
    puts $evidence "\nQUERY $label: $command"
    if {[catch {uplevel 1 $command} value options]} {
        puts $evidence "ERROR: $value"
        return {}
    }
    puts $evidence $value
    flush $evidence
    return $value
}
proc props {object} {
    foreach p [query "properties:$object" [list list_property $object]] {
        query "property:$object:$p" [list get_property $p $object]
    }
}
if {[catch {
    create_project -in_memory -part $part
    link_design -part $part
    query version {version}
    query part {get_property PART [current_design]}
    foreach command {get_nodes get_pips get_site_pins get_bel_pins report_clock_utilization} {
        query "help:$command" [list help $command]
    }
    props [get_parts $part]
    props [get_clock_regions X4Y6]
    set slices [lsort -dictionary [get_sites -filter {CLOCK_REGION == X4Y6 && SITE_TYPE =~ SLICE*}]]
    query slices [list lrange $slices 0 35]
    set slice [lindex $slices 0]
    props [get_sites $slice]
    set tile [get_tiles -of_objects [get_sites $slice]]
    props $tile
    query slice_pins [list get_site_pins -of_objects [get_sites $slice]]
    foreach pin [get_site_pins -quiet "$slice/*CLK*"] {
        props $pin
        set nodes [query "nodes:$pin" [list get_nodes -of_objects $pin]]
        foreach node $nodes {
            props $node
            query "wires:$node" [list get_wires -of_objects $node]
            query "uphill-nodes:$node" [list get_nodes -uphill -of_objects $node]
            query "uphill-pips:$node" [list get_pips -uphill -of_objects $node]
        }
    }
    set leaves [lsort -dictionary [get_sites -filter {CLOCK_REGION == X4Y6 && SITE_TYPE == BUFCE_LEAF}]]
    query leaves [list lrange $leaves 0 35]
    set leaf [get_sites [lindex $leaves 0]]
    props $leaf
    props [get_tiles -of_objects $leaf]
    query leaf_pins [list get_site_pins -of_objects $leaf]
    query leaf_bels [list get_bels -of_objects $leaf]
    foreach pin [get_site_pins -of_objects $leaf] {
        props $pin
        foreach node [query "nodes:$pin" [list get_nodes -of_objects $pin]] {
            props $node
            query "wires:$node" [list get_wires -of_objects $node]
            query "downhill-nodes:$node" [list get_nodes -downhill -of_objects $node]
            query "uphill-nodes:$node" [list get_nodes -uphill -of_objects $node]
        }
    }
    report_clock_utilization -file $out/clock_utilization.rpt
    close_design
} message options]} {
    puts $evidence "FATAL: $message\n[dict get $options -errorinfo]"
    close $evidence
    puts stderr $message
    exit 1
}
close $evidence
exit 0
