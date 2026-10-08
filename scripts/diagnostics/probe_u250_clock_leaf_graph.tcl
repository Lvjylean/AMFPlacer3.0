# Inspect actual dedicated leaf reachability of representative fabric clock pins.
set_param general.maxThreads 4
if {[llength $argv] != 2} {error "Expected part and new output directory"}
set part [lindex $argv 0]
set out [file normalize [lindex $argv 1]]
if {[file exists $out]} {error "Output exists"}
file mkdir $out
create_project -in_memory -part $part
link_design -part $part
set f [open $out/pin_leaf_reachability.tsv w]
puts $f "site\ttype\tclock_region\tpin\tstart_nodes\tleaf_count\tleaf_nodes"
set e [open $out/edges.tsv w]
puts $e "sink_pin\tdepth\tdownstream\tupstream\tupstream_intent"
set l [open $out/leaf_sources.tsv w]
puts $l "leaf_node\tsite_pins\tinput_pin\tinput_node\tupstream_nodes"
set seenLeaves [dict create]
foreach sample {SLICE_X117Y360 SLICE_X117Y389 SLICE_X117Y390 SLICE_X117Y419 SLICE_X118Y360 SLICE_X119Y360 SLICE_X120Y360 SLICE_X121Y360} {
    set site [get_sites $sample]
    foreach pin [get_site_pins -of_objects $site -filter {NAME =~ *CLK*}] {
        set frontier [get_nodes -of_objects $pin]
        set start $frontier
        set leaves {}
        for {set depth 1} {$depth <= 2} {incr depth} {
            set next {}
            foreach node $frontier {
                foreach up [get_nodes -quiet -uphill -of_objects $node] {
                    set intent [get_property INTENT_CODE_NAME $up]
                    puts $e [join [list $pin $depth $node $up $intent] "\t"]
                    if {$intent eq "NODE_GLOBAL_LEAF"} {lappend leaves $up} else {lappend next $up}
                }
            }
            set frontier [lsort -unique $next]
        }
        set leaves [lsort -unique $leaves]
        puts $f [join [list $site [get_property SITE_TYPE $site] [get_property CLOCK_REGION $site] $pin [join $start ,] [llength $leaves] [join $leaves ,]] "\t"]
        foreach leaf $leaves {
            if {[dict exists $seenLeaves $leaf]} {continue}
            dict set seenLeaves $leaf 1
            set leafPins [get_site_pins -quiet -of_objects $leaf]
            foreach leafSite [get_sites -quiet -of_objects $leafPins] {
                set input [get_site_pins -quiet "$leafSite/CLK_IN"]
                if {[llength $input] == 0} {continue}
                set inputNode [get_nodes -of_objects $input]
                puts $l [join [list $leaf [join $leafPins ,] $input $inputNode [join [get_nodes -uphill -of_objects $inputNode] ,]] "\t"]
            }
        }
    }
}
close $f
close $e
close $l
set f [open $out/site_pin_names.tsv w]
puts $f "site\ttype\tpins"
foreach type {SLICEM SLICEL DSP48E2 RAMBFIFO36 RAMB181 RAMBFIFO18 URAM288} {
    set site [lindex [get_sites -quiet -filter "CLOCK_REGION == X4Y6 && SITE_TYPE == $type"] 0]
    if {$site ne ""} {puts $f [join [list $site $type [join [get_site_pins -of_objects $site] ,]] "\t"]}
}
close $f
set f [open $out/metadata.tsv w]
puts $f "part\t[get_property PART [current_design]]\nvivado\t[version -short]\nplacement_executed\t0\nscope\ttwo-hop-reachable-dedicated-leaf-sources"
close $f
close_design
exit 0
