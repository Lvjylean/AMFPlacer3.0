# Export canonical primitive boundaries and complete hierarchical net groups.
# Internal Unisim implementation pins are excluded using the canonical leaf set.
if {$argc != 2} {error "Expected prepared full-system DCP and new inventory directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
proc properties {property objects} {
    if {![llength $objects]} {return {}}
    set n [llength $objects]
    if {$n <= 512} {return [get_property $property $objects]}
    puts "PROPERTY_BEGIN $property objects=$n";flush stdout
    set result {}
    for {set offset 0} {$offset<$n} {incr offset 512} {
        lappend result {*}[get_property $property [lrange $objects $offset [expr {$offset+511}]]]
    }
    puts "PROPERTY_END $property objects=$n";flush stdout
    return $result
}
if {[catch {
    open_checkpoint $input
    if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {error "Unexpected target part"}
    set f [open [file join $output binding.json] w]
    puts $f [format {{"input_sha256":"%s"}} [lindex [exec sha256sum -- $input] 0]]
    close $f
    set begin [clock milliseconds]
    set primitives [get_cells -hier -filter {IS_PRIMITIVE}]
    array set primitive_names {}
    foreach name [get_property NAME $primitives] {set primitive_names($name) 1}
    array set cell_ids {}
    array set cell_types {}
    set cells [open [file join $output cells.tsv] w]
    puts $cells "cell_id\tcell_name\tprimitive"
    set id 0
    foreach name [properties NAME $primitives] kind [properties REF_NAME $primitives] {
        set parent $name
        set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            if {[info exists primitive_names($parent)]} {set internal 1;break}
        }
        if {$internal} {continue}
        if {[regexp {[\s,]} $name]} {error "Unsupported delimiter in cell name: $name"}
        set cell_ids($name) $id
        set cell_types($name) $kind
        puts $cells "$id\t$name\t$kind"
        incr id
    }
    close $cells
    puts "AMF_CORE_CANONICAL_CELLS $id";flush stdout
    set nets [open [file join $output nets.tsv] w]
    puts $nets "net_id\tdrivers\tsinks\ttype"
    # Resolve the small top-level port set once. Querying get_ports on a
    # constant's huge hierarchical alias group is pathologically expensive.
    array set input_ports_by_net {}
    array set output_ports_by_net {}
    foreach port [get_ports] {
        set direction [get_property DIRECTION $port]
        foreach topnet [get_nets -quiet -top_net_of_hierarchical_group -of_objects $port] {
            if {$direction eq "IN"} {lappend input_ports_by_net($topnet) "PORT:$port"} elseif {$direction eq "OUT"} {
                lappend output_ports_by_net($topnet) "PORT:$port"
            } else {error "Unsupported bidirectional port: $port"}
        }
    }
    puts "PORT_MAP_READY";flush stdout
    set periods [open [file join $output clock_net_periods.tsv] w]
    puts $periods "net_id\tdriver\tclocks\tperiods_ns"
    set count 0
    set omitted 0
    set visited 0
    set pin_count 0
    array set visited_constants {}
    set ordered_nets [concat [get_nets -quiet {<const0> <const1>}] [get_nets -hierarchical -top_net_of_hierarchical_group]]
    foreach net $ordered_nets {
        if {$net in {<const0> <const1>}} {
            if {[info exists visited_constants($net)]} {continue}
            set visited_constants($net) 1
        }
        incr visited
        if {$visited % 10000 == 0} {puts "NET_VISIT $visited $net";flush stdout}
        set segments [get_nets -quiet -segments $net]
        if {![llength $segments]} {error "Cannot resolve hierarchical net: $net"}
        set drivers {}
        set sinks {}
        set constants {}
        set kind SIGNAL
        set nsegments [llength $segments]
        if {$nsegments>128} {puts "LARGE_NET_BEGIN $net segments=$nsegments";flush stdout}
        for {set offset 0} {$offset<$nsegments} {incr offset 128} {
        set group [lrange $segments $offset [expr {$offset+127}]]
        set pins [get_pins -quiet -of_objects $group]
        if {$nsegments>128 && $offset % 12800 == 0} {puts "LARGE_NET_PINS $offset/$nsegments pins=[llength $pins]";flush stdout}
        foreach pin [properties NAME $pins] direction [properties DIRECTION $pins] clockpin [properties IS_CLOCK $pins] {
            set split [string last / $pin]
            set cell [string range $pin 0 [expr {$split-1}]]
            if {![info exists cell_ids($cell)]} {continue}
            set endpoint "$cell_ids($cell):[string range $pin [expr {$split+1}] end]"
            if {$direction eq "OUT"} {
                lappend drivers $endpoint
                lappend constants $cell_types($cell)
            } elseif {$direction eq "IN"} {
                # A BUFG input is not proof of a real clock: it can carry reset.
                # Its output is classified from actual downstream sequential pins.
                if {($clockpin eq "1" || $clockpin eq "true") && $cell_types($cell) ne "BUFGCE"} {set kind CLOCK}
                lappend sinks $endpoint
            } else {error "Unsupported bidirectional primitive pin: $pin"}
        }
        }
        if {$nsegments>128} {puts "LARGE_NET_END $net";flush stdout}
        if {[info exists input_ports_by_net($net)]} {
            foreach port $input_ports_by_net($net) {lappend drivers $port;lappend constants PORT}
        }
        if {[info exists output_ports_by_net($net)]} {lappend sinks {*}$output_ports_by_net($net)}
        set drivers [lsort -unique $drivers]
        set sinks [lsort -unique $sinks]
        if {![llength $drivers] && ![llength $sinks]} {incr omitted;continue}
        if {[llength $drivers]>1 && [llength [lsort -unique $constants]]==1 && [lindex $constants 0] in {GND VCC}} {
            # Equivalent constant sources are represented by one constant driver.
            set drivers [lrange $drivers 0 0]
        }
        if {[llength $drivers]!=1} {error "Non-single-driver logical net $net : $drivers -> $sinks"}
        if {[llength [lsort -unique $constants]]==1 && [lindex $constants 0] in {GND VCC}} {set kind SIGNAL}
        if {$kind eq "CLOCK"} {
            set net_clocks [get_clocks -quiet -of_objects $net]
            set clock_periods {}
            if {[llength $net_clocks]} {set clock_periods [get_property PERIOD $net_clocks]}
            puts $periods "n$count\t[lindex $drivers 0]\t[join $net_clocks ,]\t[join $clock_periods ,]"
        }
        puts $nets "n$count\t[join $drivers ,]\t[join $sinks ,]\t$kind"
        incr count
        incr pin_count [expr {[llength $drivers]+[llength $sinks]}]
        if {$count % 20000 == 0} {puts "AMF_CORE_NET_EXPORT $count visited=$visited";flush stdout}
    }
    close $nets
    close $periods
    if {$count == 0 || $pin_count < $id} {error "Incomplete connectivity inventory: cells=$id nets=$count endpoints=$pin_count"}
    set f [open [file join $output dsp_preg.tsv] w]
    puts $f "cell\tPREG"
    foreach cell [get_cells -hier -filter {REF_NAME == DSP48E2}] {
        puts $f "$cell\t[get_property PREG $cell]"
    }
    close $f
    set f [open [file join $output clocks.tsv] w]
    puts $f "clock\tperiod_ns\tsource"
    foreach clk [get_clocks] {
        puts $f "$clk\t[get_property PERIOD $clk]\t[get_property SOURCE_PINS $clk]"
    }
    close $f
    set f [open [file join $output export_summary.tsv] w]
    puts $f "canonical_cells\t$id"
    puts $f "logical_nets\t$count"
    puts $f "endpoint_count\t$pin_count"
    puts $f "omitted_internal_nets\t$omitted"
    puts $f "export_seconds\t[expr {([clock milliseconds]-$begin)/1000.0}]"
    close $f
    puts "AMF_CORE_AMF_INVENTORY_EXPORTED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
