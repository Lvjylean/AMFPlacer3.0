# Read-only diagnosis of existing routed placement and timing. No place/route/write_checkpoint.
set_param general.maxThreads 4
lassign $argv input out
file mkdir $out
proc gp {object prop} {
    if {$object eq ""} {return ""}
    if {[lsearch -exact [list_property $object] $prop] < 0} {return ""}
    return [get_property $prop $object]
}
proc clean {value} {return [string map [list "\t" " " "\n" " " "\r" " "] $value]}
array set pinInfo {}
proc pin_info {pin} {
    global pinInfo
    if {[info exists pinInfo($pin)]} {return $pinInfo($pin)}
    set cell [get_cells -quiet -of_objects $pin]
    set site [get_sites -quiet -of_objects $cell]
    set result [list $cell [gp $cell REF_NAME] $site [gp $cell BEL] [get_slrs -quiet -of_objects $site] [get_clock_regions -quiet -of_objects $site] [gp $pin DIRECTION]]
    set pinInfo($pin) $result
    return $result
}
if {[catch {
    open_checkpoint $input
    set meta [open [file join $out metadata.tsv] w]
    puts $meta "vivado\t[version -short]"
    puts $meta "part\t[get_property PART [current_design]]"
    puts $meta "dcp\t$input"
    foreach c [get_clocks] {puts $meta "clock\t$c\t[get_property PERIOD $c]"}
    puts $meta "pblocks\t[llength [get_pblocks -quiet]]"
    close $meta
    puts "DIAG_STAGE get_timing_paths"
    set paths [get_timing_paths -quiet -setup -max_paths 200 -nworst 1 -no_report_unconstrained]
    report_property -all [lindex $paths 0] -file [file join $out path_properties.rpt]
    report_timing -max_paths 20 -nworst 1 -path_type full_clock_expanded -input_pins -nets -file [file join $out top20_timing.rpt]
    set pf [open [file join $out paths.tsv] w]
    puts $pf "path\tslack_ns\tdatapath_ns\tlogic_ns\tnet_ns\tstartpoint\tendpoint\tslr_crossings\tlogic_levels"
    set tf [open [file join $out points.tsv] w]
    puts $tf "path\torder\tpin\tcell\tref\tsite\tbel\tslr\tcr\tdirection\tarrival\tincr\tslew"
    set cf [open [file join $out connections.tsv] w]
    puts $cf "path\torder\tsource_pin\tsink_pin\tnet\tsource_cell\tsource_ref\tsource_site\tsource_bel\tsource_slr\tsource_cr\tsource_direction\tsink_cell\tsink_ref\tsink_site\tsink_bel\tsink_slr\tsink_cr\tsink_direction\tfanout\tnet_slow_max_ns\tpath_point_incr"
    set ef [open [file join $out gaps.tsv] w]
    puts $ef "path\torder\tpin\treason"
    array set connCache {}
    set idx 0
    foreach path $paths {
        puts $pf [join [list $idx [gp $path SLACK] [gp $path DATAPATH_DELAY] [gp $path DATAPATH_LOGIC_DELAY] [gp $path DATAPATH_NET_DELAY] [gp $path STARTPOINT_PIN] [gp $path ENDPOINT_PIN] [gp $path SLR_CROSSINGS] [gp $path LOGIC_LEVELS]] "\t"]
        set order 0
        foreach pin [get_pins -quiet -of_objects $path] {
            if {$pin eq ""} {puts $ef "$idx\t$order\t\tpoint-without-pin";incr order;continue}
            set info [pin_info $pin]
            puts $tf [join [concat [list $idx $order $pin] $info [list {} {} {}]] "\t"]
            if {[lindex $info end] eq "IN"} {
                if {![info exists connCache($pin)]} {
                    set connCache($pin) ""
                    set net [get_nets -quiet -of_objects $pin]
                    if {[llength $net] == 1 && ![llength [get_clocks -quiet -of_objects $net]]} {
                        set driver [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == OUT}]
                        if {[llength $driver] == 1} {
                            set sourceInfo [pin_info $driver]
                            if {[lindex $sourceInfo 1] ni {VCC GND}} {
                                set delays [get_net_delays -quiet -of_objects $net -to $pin]
                                if {[llength $delays] == 1} {
                                    set ps [gp $delays SLOW_MAX]
                                    if {[string is double -strict $ps]} {
                                        set fanout [llength [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]]
                                        set connCache($pin) [concat [list $driver $pin $net] $sourceInfo $info [list $fanout [expr {$ps/1000.0}]]]
                                    } else {puts $ef "$idx\t$order\t$pin\tnon-numeric-delay"}
                                } else {puts $ef "$idx\t$order\t$pin\tdelay-count-[llength $delays]"}
                            }
                        }
                    }
                }
                if {$connCache($pin) ne ""} {puts $cf [join [concat [list $idx $order] $connCache($pin) [list {}]] "\t"]}
            }
            incr order
        }
        if {$idx % 20 == 0} {puts "DIAG_PATH $idx";flush $pf;flush $tf;flush $cf}
        incr idx
    }
    close $pf;close $tf;close $cf;close $ef
    set f [open [file join $out completed.txt] w];puts $f "paths=$idx";close $f
} failure options]} {
    puts stderr [dict get $options -errorinfo]
    set f [open [file join $out failed.txt] w];puts $f [dict get $options -errorinfo];close $f
    exit 2
}
exit 0

