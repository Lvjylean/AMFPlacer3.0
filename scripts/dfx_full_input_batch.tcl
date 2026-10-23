# Full-design export using one connectivity traversal per logical net.
# Source after dfx_full_input.tcl; the incremental boundary-cone exporter is unchanged.
namespace eval amf_full {
    proc export_batched {out} {
        global dfx
        set begin [clock milliseconds]
        export_regions $out
        set all [amf_dfx::primitive_cells]
        set names [get_property NAME $all];set types [get_property REF_NAME $all]
        array set kinds {};array set selected {};array set infra {};set constants 0
        foreach c $names t $types {
            set kinds($c) $t
            if {$t in {GND VCC}} {incr constants;continue}
            if {[fabric $t]} {set selected($c) 1;continue}
            if {[region $c] ne "static" || ![regexp {^(BUFG.*|BSCANE2)$} $t]} {error "Unsupported full-design primitive: $c $t"}
            set infra($c) $t
        }
        # Retain canonical primitive pins, including hard-primitive external pins;
        # never use -leaf, which descends into DSP/RAM Unisim internals.
        array set cellPins {};array set attrs {};array set pinNet {};array set pinAlias {};array set wantedNets {}
        set nets {};set cellsCount [llength $all];set pinCount 0
        for {set offset 0} {$offset<$cellsCount} {incr offset 256} {
            set group [lrange $all $offset [expr {$offset+255}]]
            set pins [get_pins -quiet -of_objects $group]
            set fabricPins {}
            foreach obj $pins p [get_property NAME $pins] ref [get_property REF_PIN_NAME $pins] dir [get_property DIRECTION $pins] clk [get_property IS_CLOCK $pins] connected [get_property IS_CONNECTED $pins] {
                set c [amf_dfx_scope::owner $p]
                if {![info exists kinds($c)]} {error "Pin query escaped canonical primitives: $p"}
                lappend cellPins($c) $p
                set attrs($p) [list $ref $dir $clk $connected];set pinNet($p) {};set pinAlias($p) {};incr pinCount
                if {[info exists selected($c)]} {lappend fabricPins $obj}
            }
            if {[llength $fabricPins]} {
                set batchNets [get_nets -quiet -top_net_of_hierarchical_group -of_objects $fabricPins]
                foreach net $batchNets n [get_property NAME $batchNets] {
                    if {![info exists wantedNets($n)]} {set wantedNets($n) 1;lappend nets $net}
                }
            }
            if {$offset%16384==0} {puts "AMF_BATCH_PINS cells=$offset/$cellsCount pins=$pinCount nets=[llength $nets] elapsed_ms=[expr {[clock milliseconds]-$begin}]";flush stdout}
        }
        set pinDone [clock milliseconds]
        puts "AMF_BATCH_PINS_DONE cells=$cellsCount pins=$pinCount nets=[llength $nets] milliseconds=[expr {$pinDone-$begin}]";flush stdout
        array set drivers {};array set clockDrivers {};array set netType {};array set sinkCount {};array set seenAliases {}
        set index 0;set visitedAliases 0
        foreach net $nets kind [get_property TYPE $nets] n [get_property NAME $nets] {
            if {[info exists seenAliases($n)]} {continue}
            set netType($n) $kind;set sinks 0;set outs {}
            set segments [get_nets -quiet -segments $net]
            if {![llength $segments]} {error "No segments for $n"}
            # A primitive pin belongs to one local hierarchical segment. Preserve
            # that exact alias; batching pins across segments collapses aliases.
            foreach segment $segments alias [get_property NAME $segments] {
                if {[info exists seenAliases($alias)]} {error "Overlapping logical net groups: $alias"}
                set seenAliases($alias) $n;incr visitedAliases
                set pins [get_pins -quiet -of_objects $segment]
                if {![llength $pins]} {continue}
                foreach p [get_property NAME $pins] {
                    if {![info exists attrs($p)]} {continue}
                    # Hard primitive external pins also appear on internal Unisim
                    # segments. Keep the outer connection, as scalar get_nets does.
                    if {[string first "[amf_dfx_scope::owner $p]/" $alias]==0} {continue}
                    if {$pinNet($p) ne ""} {error "Canonical pin belongs to multiple segments: $p $pinAlias($p) $alias"}
                    set pinNet($p) $n;set pinAlias($p) $alias
                    set dir [lindex $attrs($p) 1]
                    if {$dir eq "OUT"} {lappend outs $p} elseif {$dir eq "IN"} {incr sinks} else {error "Unsupported pin direction at $p"}
                }
            }
            if {$kind eq "GROUND"} {set drivers($n) <const0>} elseif {$kind eq "POWER"} {set drivers($n) <const1>} else {
                if {[llength $outs]!=1} {error "Expected one full-design driver at $n: $outs"}
                set drivers($n) [lindex $outs 0]
            }
            set clockDrivers($n) $outs;set sinkCount($n) $sinks
            incr index
            if {$index%16384==0} {puts "AMF_BATCH_NETS logical_nets=$index requested_aliases=[llength $nets] visited_aliases=$visitedAliases elapsed_ms=[expr {[clock milliseconds]-$pinDone}]";flush stdout}
        }
        set netDone [clock milliseconds]
        puts "AMF_BATCH_NETS_DONE nets=$index milliseconds=[expr {$netDone-$pinDone}]";flush stdout
        set cf [open $out/cells.tsv w];puts $cf "cell\tprimitive\tregion"
        set nf [open $out/netlist.txt w];array set clocks {}
        set inf [open $out/infrastructure.tsv w];puts $inf "cell\tprimitive\tplacement_owner"
        foreach c [lsort [array names infra]] {puts $inf "$c\t$infra($c)\tVivado dedicated clock/configuration placement"};close $inf
        set srl [open $out/srl_controls.tsv w];puts $srl "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
        set boundary [open $out/dedicated_interface_pins.tsv w];puts $boundary "fabric_pin\tdedicated_driver"
        set dsp [open $out/dsp_registered_outputs.txt w];puts $dsp "AMF_DSP_REGISTERED_OUTPUTS 1"
        set count 0
        foreach c $names obj $all {
            if {![info exists selected($c)]} {continue}
            set t $kinds($c)
            puts $cf "$c\t$t\t[region $c]";puts $nf "curCell=> $c type=> $t"
            set certified 0
            if {$t eq "DSP48E2"} {set certified [expr {[get_property PREG $obj]==1}]}
            if {![info exists cellPins($c)]} {set cellPins($c) {}}
            foreach p $cellPins($c) {
                lassign $attrs($p) ref dir clk connected
                if {$connected && $pinNet($p) eq ""} {error "Connected fabric pin absent from net traversal: $p"}
                if {!$connected && $pinNet($p) ne ""} {error "Unconnected fabric pin unexpectedly mapped: $p"}
                set n $pinNet($p);set driver {}
                if {$n ne ""} {
                    if {$netType($n) eq "GLOBAL_CLOCK"} {set clk 1}
                    set driver $drivers($n)
                    if {$clk} {
                        if {[llength $clockDrivers($n)]!=1} {error "Expected one clock driver at $n: $clockDrivers($n)"}
                        set driver @DFX_CLOCK/[lindex $clockDrivers($n) 0]
                        if {$dir eq "IN"} {set clocks($driver) 1}
                    } elseif {$driver ni {<const0> <const1>}} {
                        set owner [amf_dfx_scope::owner $driver]
                        if {![info exists selected($owner)]} {
                            if {![info exists infra($owner)]} {error "Missing full AMF data driver: $p $driver"}
                            puts $boundary "$p\t$driver"
                        }
                    }
                    if {$t eq "DSP48E2" && $dir eq "OUT" && !$clk && $sinkCount($n)>0 && ![regexp {^P\[([0-9]|[1-3][0-9]|4[0-7])\]$} $ref]} {set certified 0}
                }
                puts $nf " pin=> $p refpin=> $ref dir=> $dir net=> $pinAlias($p) drivepin=> $driver"
            }
            if {$t in {SRL16E SRLC32E}} {
                set inv [amf_dfx_srl::inversion $obj $t]
                puts $srl [join [list $c $t - - $inv [get_property IS_LOC_FIXED $obj] [get_property IS_BEL_FIXED $obj] -] \t]
            }
            if {$certified} {puts $dsp "$c PREG=1"}
            incr count
            if {$count%32768==0} {puts "AMF_BATCH_WRITE cells=$count/[array size selected]";flush stdout}
        }
        close $cf;close $nf;close $srl;close $boundary;close $dsp
        set f [open $out/clocks w];puts $f [join [lsort [array names clocks]] \n];close $f
        set f [open $out/full_input_coverage.tsv w]
        puts $f "canonical_primitives\t$cellsCount";puts $f "amf_fabric_primitives\t[array size selected]"
        puts $f "dedicated_infrastructure\t[array size infra]";puts $f "constants\t$constants";puts $f "static_fabric_reused\t0";close $f
        if {$cellsCount != [array size selected]+[array size infra]+$constants} {error "Full-design inventory mismatch"}
        set f [open $out/export_profile.tsv w]
        puts $f "stage\tseconds"
        puts $f "regions_cells_pins\t[expr {($pinDone-$begin)/1000.0}]"
        puts $f "logical_net_connectivity\t[expr {($netDone-$pinDone)/1000.0}]"
        puts $f "write_and_metadata\t[expr {([clock milliseconds]-$netDone)/1000.0}]";close $f
    }
}
