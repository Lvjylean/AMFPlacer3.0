# Read-only delay extraction: input.dcp requests.tsv output-directory.
if {$argc != 3} {error "Expected input.dcp requests.tsv output-directory"}
lassign $argv input requests out
set_param general.maxThreads 4
proc capture_sa_delay {delay} {
    upvar 1 records records netNames netNames measured measured ef ef
    set sink [get_property TO_PIN $delay]
    if {![info exists records($sink)]} {return}
    if {[get_property NET $delay] ne $netNames($sink)} {error "Delay net mismatch for $sink"}
    if {[info exists measured($sink)]} {error "Multiple delays for $sink"}
    set ps [get_property SLOW_MAX $delay]
    if {[get_property ESTIMATED $delay] || ![string is double -strict $ps] || $ps<=0} {
        puts $ef "[lindex $records($sink) 0]\t$sink\testimated-or-invalid-delay";return
    }
    set measured($sink) $ps
}
proc extract_sa_ratio_samples {input requests out} {
    open_checkpoint $input
    set sf [open [file join $out samples.tsv] w]
    set ef [open [file join $out gaps.tsv] w]
    puts $sf "source_pin\tsink_pin\tsource_site\tsink_site\tsource_type\tsink_type\tfanout\tdelay_ns\tstratum\tpopulation\tsample_count"
    puts $ef "source_pin\tsink_pin\treason"
    set f [open $requests r];gets $f header
    set count 0;set ok 0
    set nets {};set orderedSinks {}
    array set records {};array set netNames {};array set sinkObjects {}
    while {[gets $f line]>=0} {
        lassign [split $line "\t"] source sink dx dy slr af bf fo stratum population sampleCount
        incr count
        if {[catch {
            set sp [get_pins -quiet $source];set tp [get_pins -quiet $sink]
            if {[llength $sp]!=1 || [llength $tp]!=1} {error "missing-pin"}
            if {[get_property NAME $sp] ne $source || [get_property NAME $tp] ne $sink} {error "pin-name-mismatch"}
            set net [get_nets -quiet -of_objects $tp]
            if {[llength $net]!=1} {error "missing-net"}
            set driver [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == OUT}]
            if {[llength $driver]!=1 || [get_property NAME $driver] ne $source} {error "driver-changed"}
            if {[llength [get_clocks -quiet -of_objects $net]]} {error "clock-net"}
            set actualFo [llength [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]]
            if {$actualFo<1 || $actualFo>8} {error "fanout-out-of-range"}
            set sc [get_cells -quiet -of_objects $sp];set tc [get_cells -quiet -of_objects $tp]
            set a [get_property LOC $sc];set b [get_property LOC $tc]
            if {$a eq "" || $b eq ""} {error "unplaced"}
            if {[info exists records($sink)]} {error "duplicate-request"}
            set records($sink) [list $source $sink $a $b [get_property REF_NAME $sc] [get_property REF_NAME $tc] $actualFo $stratum $population $sampleCount]
            set netNames($sink) [get_property NAME $net]
            set sinkObjects($sink) $tp
            lappend nets {*}$net
            lappend orderedSinks $sink
        } failure]} {puts $ef "$source\t$sink\t[string map [list \n { } \t { }] $failure]"}
        if {$count%2000==0} {puts "SA_RATIO_PREPARE requested=$count eligible=[array size records]";flush stdout;flush $ef}
    }
    if {![llength $nets]} {error "No valid sampling nets"}
    # A Tcl list of names is not a Vivado native object collection. Reacquire
    # that collection before querying; do not pass the string list as objects.
    set start [clock milliseconds]
    array set measured {}
    set queried 0
    set queryMode native-collection
    if {[catch {
        set objects [get_nets -quiet $nets]
        if {[llength $objects]!=[llength $nets]} {error "Native collection size mismatch"}
        foreach delay [get_net_delays -of_objects $objects] {capture_sa_delay $delay}
        set queried [llength $objects]
    } batchFailure]} {
        puts "SA_RATIO_COLLECTION_FALLBACK [string range $batchFailure 0 300]"
        array unset measured
        set queryMode point-fallback
        foreach sink $orderedSinks {
            # This query form was verified by the initial point profiler.
            set tp [get_pins -quiet $sink]
            set net [get_nets -quiet -of_objects $tp]
            set delay [get_net_delays -of_objects $net -to $tp]
            if {[llength $delay]!=1} {error "Nonunique point delay for $sink"}
            capture_sa_delay $delay
            incr queried
            if {$queried%100==0} {puts "SA_RATIO_QUERIED nets=$queried milliseconds=[expr {[clock milliseconds]-$start}]";flush stdout}
        }
    }
    puts "SA_RATIO_NETS mode=$queryMode count=$queried milliseconds=[expr {[clock milliseconds]-$start}]"
    set checked 0
    foreach sink $orderedSinks {
        if {![info exists measured($sink)]} {puts $ef "[lindex $records($sink) 0]\t$sink\tno-routed-delay";continue}
        # Verify the whole-net/object join against five filtered point queries.
        if {$checked<5} {
            set tp [get_pins -quiet $sink]
            set net [get_nets -quiet -of_objects $tp]
            set single [get_net_delays -of_objects $net -to $tp]
            if {[llength $single]!=1 || [get_property SLOW_MAX $single]!=$measured($sink)} {error "Whole-net/point delay disagreement"}
            incr checked
        }
        set row $records($sink)
        puts $sf [join [linsert $row 7 [expr {$measured($sink)/1000.0}]] "\t"]
        incr ok
    }
    close $f;close $sf;close $ef
    set f [open [file join $out tools.tsv] w]
    puts $f "vivado\t[version -short]\npart\t[get_property PART [current_design]]\nrequested\t$count\naccepted\t$ok\nquery_mode\t$queryMode\nwhole_net_point_crosschecks\t$checked\nproperty\tNET_DELAY.SLOW_MAX, ps converted to ns"
    close $f
    puts "SA_RATIO_SAMPLE_DONE requested=$count accepted=$ok"
}
if {[catch {extract_sa_ratio_samples $input $requests $out} failure options]} {
    puts stderr [dict get $options -errorinfo];exit 2
}
exit 0
