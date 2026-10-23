# Sourceable from the full backend, or: vivado -source this.tcl -tclargs routed.dcp output-dir.
proc export_boundary_timing_samples {out {limit 200}} {
    file mkdir $out
    set pf [open [file join $out timing_paths.tsv] w]
    set sf [open [file join $out timing_connections.tsv] w]
    set ef [open [file join $out timing_sample_gaps.tsv] w]
    puts $pf "path\tslack_ns\tdatapath_ns\tstartpoint\tendpoint"
    puts $sf "path\torder\tsource_pin\tsink_pin\tsource_site\tsink_site\tsource_type\tsink_type\tfanout\trouted_delay_ns"
    puts $ef "path\tpin\treason"
    set pathId 0
    array set cachedConnections {}
    # Constrained setup paths only. No synthetic HD.CLK_SRC is installed.
    foreach path [get_timing_paths -quiet -setup -max_paths $limit -nworst 1 -no_report_unconstrained] {
        puts $pf [join [list $pathId [get_property SLACK $path] [get_property DATAPATH_DELAY $path] [get_property STARTPOINT_PIN $path] [get_property ENDPOINT_PIN $path]] "\t"]
        set order 0
        foreach pin [get_pins -quiet -of_objects $path -filter {DIRECTION == IN}] {
            if {[info exists cachedConnections($pin)]} {
                if {$cachedConnections($pin) ne ""} {
                    puts $sf [join [concat [list $pathId $order] $cachedConnections($pin)] "\t"]
                    incr order
                }
                continue
            }
            set cachedConnections($pin) ""
            if {[catch {
                set net [get_nets -quiet -of_objects $pin]
                if {[llength $net]!=1} {continue}
                if {[llength [get_clocks -quiet -of_objects $net]]} {continue}
                set driver [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == OUT}]
                if {[llength $driver]!=1} {continue}
                set source [get_cells -quiet -of_objects $driver]
                set sink [get_cells -quiet -of_objects $pin]
                set st [get_property REF_NAME $source]
                if {$st in {VCC GND}} {continue}
                set a [get_property LOC $source];set b [get_property LOC $sink]
                if {$a eq "" || $b eq ""} {continue}
                set delays [get_net_delays -quiet -of_objects $net -to $pin]
                if {[llength $delays]!=1} {puts $ef "$pathId\t$pin\tno-unique-routed-delay";continue}
                set ps [get_property SLOW_MAX $delays]
                if {![string is double -strict $ps]} {puts $ef "$pathId\t$pin\tinvalid-delay";continue}
                set fanout [llength [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]]
                # NET_DELAY.SLOW_MAX is in ps; timing path properties are in ns.
                set cachedConnections($pin) [list $driver $pin $a $b $st [get_property REF_NAME $sink] $fanout [expr {$ps/1000.0}]]
                puts $sf [join [concat [list $pathId $order] $cachedConnections($pin)] "\t"]
                incr order
            } failure]} {puts $ef "$pathId\t$pin\t[string map [list \n { } \t { }] $failure]"}
        }
        incr pathId
    }
    close $pf;close $sf;close $ef
    set f [open [file join $out sample_metadata.tsv] w]
    puts $f "key\tvalue"
    puts $f "vivado_version\t[version -short]"
    puts $f "part\t[get_property PART [current_design]]"
    puts $f "max_paths\t$limit"
    puts $f "paths\t$pathId"
    puts $f "connection_delay_property\tNET_DELAY.SLOW_MAX (ps converted to ns)"
    puts $f "scope\tconstrained setup paths; cell-site geometry, not route/SLL occupancy"
    close $f
    puts "AMF_BOUNDARY_TIMING_SAMPLES paths=$pathId"
}
if {[info exists ::argc] && $::argc==2 && [string match *.dcp [lindex $::argv 0]]} {
    lassign $argv input output
    if {[catch {open_checkpoint $input;export_boundary_timing_samples $output} failure options]} {
        puts stderr [dict get $options -errorinfo];exit 2
    }
    exit 0
}
