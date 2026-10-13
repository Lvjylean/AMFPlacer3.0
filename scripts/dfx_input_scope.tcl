# Incremental AMF input: active RM plus only fixed timing cones crossing its interface.
# Static routes and partition pins remain in the Vivado DCP. No whole-design cell scan.
namespace eval amf_dfx_scope {
    variable types {}; variable netCache {}; variable pinCache {}; variable terminalCache {}
    variable selected {}; variable keep {}; variable up {}; variable down {}
    variable seenUp {}; variable seenDown {}; variable dspCertificates {}

    proc type {cell} {
        variable types
        if {![dict exists $types $cell]} {dict set types $cell [get_property REF_NAME [get_cells $cell]]}
        return [dict get $types $cell]
    }
    proc owner {pin} {return [string range $pin 0 [expr {[string last / $pin]-1}]]}
    proc canonical_pins {net dir} {
        set result {}
        set segments [get_nets -quiet -segments $net]
        if {![llength $segments]} {error "No segments for $net"}
        for {set i 0} {$i<[llength $segments]} {incr i 128} {
            foreach p [get_pins -quiet -of_objects [lrange $segments $i [expr {$i+127}]] -filter "DIRECTION == $dir"] {
                set c [get_cells -quiet -of_objects $p]
                if {[llength $c]!=1} {continue}
                if {![get_property IS_PRIMITIVE $c] || [get_property PRIMITIVE_LEVEL $c] eq "INTERNAL"} {continue}
                lappend result [get_property NAME $p]
            }
        }
        return [lsort -unique $result]
    }
    proc net {name {withSinks 0}} {
        variable netCache
        if {![dict exists $netCache $name]} {
            # Global constant aliases can span the entire design. TYPE is enough:
            # never enumerate thousands of static GND/VCC drivers or sinks.
            set category [get_property TYPE $name]
            if {$category in {GROUND POWER}} {
                if {$category eq "GROUND"} {set d <const0>;set t GND} else {set d <const1>;set t VCC}
                dict set netCache $name [list $d $t {} 1]
                return [list $d $t {}]
            }
            set drivers [canonical_pins $name OUT]
            set kinds {}
            foreach p $drivers {lappend kinds [type [owner $p]]}
            set kinds [lsort -unique $kinds]
            if {[llength $drivers]>1 && [llength $kinds]==1 && [lindex $kinds 0] in {GND VCC}} {set drivers [lrange $drivers 0 0]}
            if {[llength $drivers]!=1} {error "Expected one canonical driver at $name: $drivers"}
            set driver [lindex $drivers 0]
            set t [type [owner $driver]]
            # Power fanout is not a timing cone and must not expand the static design.
            dict set netCache $name [list $driver $t {} 0]
        }
        lassign [dict get $netCache $name] driver t sinks haveSinks
        if {$withSinks && !$haveSinks} {
            if {$t ni {GND VCC}} {set sinks [canonical_pins $name IN]}
            dict set netCache $name [list $driver $t $sinks 1]
        }
        return [lrange [dict get $netCache $name] 0 2]
    }
    proc pins {cell} {
        variable pinCache
        if {![dict exists $pinCache $cell]} {
            set rows {}
            foreach p [get_pins -of_objects [get_cells $cell]] {
                set n [get_nets -quiet -top_net_of_hierarchical_group -of_objects $p]
                if {[llength $n]>1} {error "Multiple nets at $p"}
                set clk [get_property IS_CLOCK $p]
                if {[llength $n] && [get_property TYPE $n] eq "GLOBAL_CLOCK"} {set clk 1}
                lappend rows [list [get_property NAME $p] [get_property REF_PIN_NAME $p] [get_property DIRECTION $p] $n $clk]
            }
            dict set pinCache $cell $rows
        }
        return [dict get $pinCache $cell]
    }
    proc terminal {cell} {
        variable terminalCache; variable dspCertificates
        if {[dict exists $terminalCache $cell]} {return [dict get $terminalCache $cell]}
        set t [type $cell]
        set yes [expr {[regexp {^(FD(RE|SE|CE|PE)|LDCE|AND2B1L|SRL16E|SRLC32E|RAM.*|FIFO(18|36)E2|URAM288.*)$} $t]}]
        if {$t eq "DSP48E2"} {
            # Mirror ordinary Pro's conservative PREG/used-output certification.
            # Do not cut a combinational DSP just because it is a hard resource.
            set preg [get_property PREG [get_cells $cell]]
            set yes [expr {$preg==1}]; set used {}
            foreach row [pins $cell] {
                lassign $row p ref dir n clk
                if {$dir ne "OUT" || $n eq "" || $clk} {continue}
                lassign [net $n 1] driver kind sinks
                if {![llength $sinks]} {continue}
                lappend used $ref
                if {![regexp {^P\[([0-9]|[1-3][0-9]|4[0-7])\]$} $ref]} {set yes 0}
            }
            dict set dspCertificates $cell [list $preg [lsort $used] $yes]
            global dfx
            if {![info exists dfx(dsp_critical)] || !$dfx(dsp_critical)} {set yes 1}
        }
        dict set terminalCache $cell $yes
        return $yes
    }
    proc include {cell reason} {
        variable selected
        if {[type $cell] in {GND VCC}} {return}
        if {![regexp {^(LUT[1-6](_2)?|FD(RE|SE|CE|PE)|LDCE|AND2B1L|CARRY8|MUXF[789]|SRL(C32E|16E)|RAM.*|FIFO(18|36)E2|DSP48E2|URAM288.*)$} [type $cell]]} {
            error "Unsupported primitive in required RM/interface cone: $cell ([type $cell])"
        }
        dict set selected $cell $reason 1
    }
    proc input {pin n} {
        variable keep; variable up
        if {$n eq ""} {return}
        lassign [net $n] driver kind sinks
        dict set keep $pin 1
        if {$kind in {GND VCC}} {return}
        set c [owner $driver]
        include $c input_cone
        dict set keep $driver 1
        if {![amf_dfx::active $c]} {lappend up $c}
    }
    proc output {pin n} {
        variable keep; variable down
        if {$n eq ""} {return}
        lassign [net $n 1] driver kind sinks
        if {$driver ne $pin} {error "Output/driver mismatch: $pin $driver"}
        foreach sink $sinks {
            set c [owner $sink]
            include $c output_cone
            dict set keep $pin 1; dict set keep $sink 1
            if {![amf_dfx::active $c]} {lappend down $c}
        }
    }
    proc select {activeCells} {
        variable selected; variable keep; variable up; variable down
        variable seenUp; variable seenDown
        set scanned 0
        foreach c $activeCells {
            incr scanned
            if {$scanned % 512 == 0} {puts "AMF_DFX_SCOPE_SCAN active=$scanned/[llength $activeCells]"}
            if {[type $c] in {GND VCC}} {continue}
            include $c active
            foreach row [pins $c] {
                lassign $row p ref dir n clk
                dict set keep $p 1
                if {$clk || $n eq ""} {continue}
                if {$dir eq "IN"} {input $p $n} elseif {$dir eq "OUT"} {output $p $n} else {error "Unsupported INOUT pin $p"}
            }
        }
        set ui 0; set di 0
        while {$ui<[llength $up] || $di<[llength $down]} {
            if {$ui<[llength $up]} {
                set c [lindex $up $ui]; incr ui
                if {[dict exists $seenUp $c]} {continue}; dict set seenUp $c 1
                if {[terminal $c]} {continue}
                foreach row [pins $c] {
                    lassign $row p ref dir n clk
                    if {$dir eq "IN" && !$clk} {input $p $n}
                }
            } else {
                set c [lindex $down $di]; incr di
                if {[dict exists $seenDown $c]} {continue}; dict set seenDown $c 1
                if {[terminal $c]} {continue}
                # Preserve side-input arrival context at downstream combinational cells.
                lappend up $c
                foreach row [pins $c] {
                    lassign $row p ref dir n clk
                    if {$dir eq "OUT" && !$clk} {output $p $n}
                }
            }
        }
        return $selected
    }
    proc active_cells {} {
        global dfx
        set before [current_instance]
        current_instance $dfx(active_partition)
        set code [catch {
            set result {}
            foreach c [get_cells -hier -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}] {
                lappend result [get_property NAME $c]
            }
        } value opts]
        current_instance
        if {$before ne ""} {current_instance $before}
        if {$code} {return -options $opts $value}
        foreach c $result {if {![amf_dfx::active $c]} {error "Scoped query escaped active RP: $c"}}
        if {![llength $result]} {error "Active RM has no primitives"}
        return [lsort $result]
    }
    proc export {out modeled} {
        variable selected; variable keep; variable dspCertificates
        set activeCells [amf_dfx::timed active_cell_query {active_cells}]
        amf_dfx::timed interface_cone_selection {select $activeCells}
        puts "AMF_DFX_SCOPE selected=[dict size $selected] raw_active=[llength $activeCells]"
        set cellsf [open $out/cells.tsv w]; puts $cellsf "cell\tprimitive\tactive"
        set staticf [open $out/static_placement.tsv w]; puts $staticf "cell\tsite\tbel"
        set contextf [open $out/interface_context.tsv w]; puts $contextf "cell\tprimitive\treason\ttiming_endpoint"
        set activeCount 0; set fixedCount 0
        amf_dfx::timed scoped_cell_metadata {
            foreach c [lsort [dict keys $selected]] {
                if {[regexp {\s} $c]} {error "Whitespace in AMF primitive name: $c"}
                if {[amf_dfx::active $c]} {
                    puts $cellsf "$c\t[type $c]\t1"; incr activeCount
                } else {
                    set loc [get_property LOC [get_cells $c]]; set bel [get_property BEL [get_cells $c]]
                    if {![dict exists $modeled $loc] || $bel eq ""} {error "Required fixed interface cell has no modeled position: $c $loc $bel"}
                    puts $staticf "$c\t$loc\t$bel"; incr fixedCount
                    puts $contextf "$c\t[type $c]\t[join [lsort [dict keys [dict get $selected $c]]] ,]\t[terminal $c]"
                }
            }
        }
        close $cellsf; close $staticf; close $contextf
        set f [open $out/srl_controls.tsv w]
        puts $f "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
        set srlCount 0
        amf_dfx::timed active_srl_metadata {
            foreach c $activeCells {
                if {[type $c] ni {SRL16E SRLC32E}} {continue}
                incr srlCount
                set invert [get_property -quiet IS_CLK_INVERTED [get_cells $c]]
                if {$invert eq ""} {set invert UNKNOWN}
                # CLK/CE/Q31 connectivity is already read from the actual AMF netlist.
                puts $f [join [list $c [type $c] - - $invert [get_property IS_LOC_FIXED [get_cells $c]] [get_property IS_BEL_FIXED [get_cells $c]] -] "\t"]
            }
        }
        close $f
        set f [open $out/netlist.txt w]; set clocks {}; set pinCount 0
        amf_dfx::timed scoped_netlist_export {
            foreach c [lsort [dict keys $selected]] {
                puts $f "curCell=> $c type=> [type $c]"
                foreach row [pins $c] {
                    lassign $row p ref dir n clk
                    if {![amf_dfx::active $c] && ![dict exists $keep $p] && !$clk} {continue}
                    set driver {}
                    if {$n ne ""} {
                        if {$clk} {
                            # An external clock identifier, not a fabricated physical cell.
                            set driver @DFX_CLOCK/$n
                            if {$dir eq "IN"} {dict set clocks $driver 1}
                        } else {
                            lassign [net $n] driver kind sinks
                            if {$kind eq "GND"} {set driver <const0>} elseif {$kind eq "VCC"} {set driver <const1>} elseif {![dict exists $selected [owner $driver]] || ![dict exists $keep $driver]} {
                                error "Unrepresented driver of retained data pin: $p $driver"
                            }
                        }
                    }
                    puts $f " pin=> $p refpin=> $ref dir=> $dir net=> $n drivepin=> $driver"
                    incr pinCount
                }
                if {[type $c] eq "DSP48E2"} {terminal $c}
            }
        }
        close $f
        set f [open $out/dsp_registered_outputs.txt w]; puts $f "AMF_DSP_REGISTERED_OUTPUTS 1"
        set audit [open $out/dsp_certification.tsv w]; puts $audit "cell\tPREG\tused_outputs\tcertified"
        dict for {c data} $dspCertificates {
            if {![dict exists $selected $c]} {continue}
            lassign $data preg used certified
            if {$certified} {puts $f "$c PREG=1"}
            puts $audit "$c\t$preg\t$used\t$certified"
        }
        close $f; close $audit
        set f [open $out/clocks w]; puts $f [join [lsort [dict keys $clocks]] \n]; close $f
        set f [open $out/input_scope.tsv w]
        puts $f "schema\trm-boundary-cone-v1"
        foreach {k v} [list active_cells $activeCount fixed_interface_cells $fixedCount active_srl_cells $srlCount retained_pins $pinCount whole_static_scan 0] {puts $f "$k\t$v"}
        puts $f "timing_model\tExisting AMF estimated delays on retained real-cell interface cones; not routed Vivado boundary budgets"
        close $f
        puts "AMF_DFX_PREPARED active=$activeCount fixed_interface=$fixedCount srl=$srlCount pins=$pinCount"
    }
}
