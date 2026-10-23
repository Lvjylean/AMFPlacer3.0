# Adapter boundary only: export primitive/port metadata. Static selection,
# cache loading, connectivity replacement and masks belong to native AMF.
source [file join [file dirname [info script]] dfx_input_scope.tcl]
namespace eval amf_native_export {
    variable driverCache
    proc fabric {t} {
        return [regexp {^(LUT[1-6](_2)?|FD(RE|SE|CE|PE)|LDCE|AND2B1L|CARRY8|MUXF[789]|SRL(C32E|16E)|RAM.*|FIFO(18|36)E2|DSP48E2|URAM288.*)$} $t]
    }
    proc driver {n} {
        variable driverCache
        if {$n eq ""} {return {}}
        if {[info exists driverCache($n)]} {return $driverCache($n)}
        set type [get_property TYPE [get_nets $n]]
        if {$type eq "GROUND"} {set d <const0>} elseif {$type eq "POWER"} {set d <const1>} else {
            set d [amf_dfx_scope::canonical_pins $n OUT]
            set ports [get_ports -quiet -of_objects [get_nets -segments $n] -filter {DIRECTION == IN}]
            foreach p $ports {lappend d @PORT/[get_property NAME $p]}
            set d [lsort -unique $d]
            if {[llength $d]!=1} {error "Native export requires one represented driver: $n $d"}
            set d [lindex $d 0]
        }
        set driverCache($n) $d
        return $d
    }
    proc is_clock {p n} {
        if {[get_property IS_CLOCK $p]} {return 1}
        return [expr {$n ne "" && [get_property TYPE [get_nets $n]] eq "GLOBAL_CLOCK"}]
    }
    proc module {out id} {
        variable driverCache; array unset driverCache
        set cells [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}]
        set nf [open $out/netlist.txt w]
        set pf [open $out/ports.tsv w];puts $pf "module\tport\tdirection\tdriver\tclock"
        set sf [open $out/srl_controls.tsv w];puts $sf "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
        set df [open $out/dsp_registered_outputs.txt w];puts $df "AMF_DSP_REGISTERED_OUTPUTS 1"
        set portClocks [dict create]
        foreach c $cells name [get_property NAME $cells] t [get_property REF_NAME $cells] {
            if {$t in {GND VCC}} {continue}
            if {![fabric $t]} {error "Unsupported primitive in OOC RM: $name $t"}
            if {[regexp {\s} $name]} {error "Whitespace in primitive name"}
            puts $nf "curCell=> $name type=> $t"
            set preg 0
            if {$t eq "DSP48E2"} {set preg [expr {[get_property PREG $c]==1}]}
            foreach p [get_pins -of_objects $c] {
                set pn [get_property NAME $p];set ref [get_property REF_PIN_NAME $p];set dir [get_property DIRECTION $p]
                if {$dir ni {IN OUT}} {error "Unsupported primitive direction: $pn"}
                set n [get_nets -quiet -top_net_of_hierarchical_group -of_objects $p]
                if {[llength $n]>1} {error "Ambiguous pin net: $pn"}
                set clk [is_clock $p $n];set d [driver $n]
                if {$clk && $d ne ""} {
                    if {[string first @PORT/ $d]==0} {dict set portClocks [string range $d 6 end] 1} else {set d @DFX_CLOCK/$d}
                }
                if {$t eq "DSP48E2" && $dir eq "OUT" && $n ne "" && !$clk && ![regexp {^P\[([0-9]|[1-3][0-9]|4[0-7])\]$} $ref]} {
                    set segments [get_nets -segments $n]
                    if {[llength [get_pins -quiet -of_objects $segments -filter {DIRECTION == IN}]] || [llength [get_ports -quiet -of_objects $segments -filter {DIRECTION == OUT}]]} {set preg 0}
                }
                puts $nf " pin=> $pn refpin=> $ref dir=> $dir net=> $n drivepin=> $d"
            }
            if {$t in {SRL16E SRLC32E}} {
                puts $sf [join [list $name $t - - [amf_dfx_srl::inversion $c $t] [get_property IS_LOC_FIXED $c] [get_property IS_BEL_FIXED $c] -] \t]
            }
            if {$preg} {puts $df "$name PREG=1"}
        }
        foreach p [get_ports] {
            set name [get_property NAME $p];set dir [get_property DIRECTION $p]
            set n [get_nets -quiet -of_objects $p];set d [driver $n]
            set clk [expr {[dict exists $portClocks $name] || [llength [get_clocks -quiet -of_objects $p]]>0}]
            if {$d eq ""} {set d -}
            puts $pf [join [list $id $name $dir $d $clk] \t]
        }
        close $nf;close $pf;close $sf;close $df
    }
    proc under_rp {name} {
        global dfx
        foreach row $dfx(rps) {lassign $row id rp; if {[string first "$rp/" $name]==0} {return 1}}
        return 0
    }
    proc partition_nodes {out} {
        global dfx
        set f [open $out/partition_nodes.tsv w];puts $f "module\tport\tdirection\tnode"
        foreach row $dfx(rps) {
            lassign $row id rp
            foreach p [get_pins -of_objects [get_cells $rp]] {
                set nodes [get_property HD.ASSIGNED_PPLOCS $p]
                if {![llength $nodes]} {set nodes [list -]}
                foreach node $nodes {
                    puts $f [join [list $id [get_property REF_PIN_NAME $p] [get_property DIRECTION $p] $node] \t]
                }
            }
        };close $f
    }
    proc full_interfaces {out} {
        global dfx
        set rows {};set outputDrivers [dict create]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            foreach p [get_pins -of_objects [get_cells $rp]] {
                set name [get_property NAME $p];set port [string range $name [expr {[string length $rp]+1}] end]
                set dir [get_property DIRECTION $p]
                set n [get_nets -quiet -of_objects $p]
                if {[llength $n]>1} {error "Multiple boundary nets: $name"}
                set clk [is_clock $p $n];set d {}
                if {$n ne ""} {
                    set kind [get_property TYPE $n]
                    if {$kind eq "GROUND"} {set d <const0>} elseif {$kind eq "POWER"} {set d <const1>} else {
                        set ds [amf_dfx_scope::canonical_pins $n OUT]
                        if {[llength $ds]!=1} {error "Ambiguous full boundary driver: $name $ds"}
                        set d [lindex $ds 0]
                        if {$clk} {set d @DFX_CLOCK/$d}
                    }
                }
                if {$dir eq "OUT" && $d ne ""} {dict lappend outputDrivers $d [list $id $port $n]}
                lappend rows [list $id $port $dir [expr {$d eq ""?"-":$d}] $clk]
            }
        }
        set pf [open $out/native_ports.tsv w];puts $pf "module\tport\tdirection\tdriver\tclock"
        foreach row $rows {puts $pf [join $row \t]};close $pf
        set bf [open $out/native_boundary_pins.tsv w];puts $bf "pin\tmodule\tport"
        dict for {driver ports} $outputDrivers {
            # Canonical netlists cannot reconstruct which named output fed a
            # static pin after distinct output nets have been merged. Reject
            # such a cache; never infer identity from TYPE or matching counts.
            if {[llength $ports]!=1 || $driver in {<const0> <const1>}} {
                error "Native interface mapping needs preserved outer-net connectivity for merged/constant outputs: $ports"
            }
            lassign [lindex $ports 0] id port n
            foreach p [amf_dfx_scope::canonical_pins $n IN] {
                if {![under_rp $p]} {puts $bf [join [list $p $id $port] \t]}
            }
        }
        close $bf
    }
}
