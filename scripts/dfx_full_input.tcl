# Complete fabric input. Dedicated configuration/clock primitives are explicitly
# inventoried and retained in the DCP; no static fabric is reused or omitted.
source [file join [file dirname [info script]] dfx_input_scope.tcl]
namespace eval amf_full {
    variable driverCache; array set driverCache {}
    variable typeOf; array set typeOf {}
    proc driver {name} {
        variable driverCache; variable typeOf
        if {![info exists driverCache($name)]} {
            set category [get_property TYPE $name]
            if {$category eq "GROUND"} {set result [list <const0> GND]} elseif {$category eq "POWER"} {
                set result [list <const1> VCC]
            } else {
                set pins [amf_dfx_scope::canonical_pins $name OUT]
                if {[llength $pins]!=1} {error "Expected one full-design driver at $name: $pins"}
                set pin [lindex $pins 0];set owner [amf_dfx_scope::owner $pin]
                if {![info exists typeOf($owner)]} {error "Driver outside canonical design: $pin"}
                set result [list $pin $typeOf($owner)]
            }
            set driverCache($name) $result
        }
        return $driverCache($name)
    }

    proc region {name} {
        global dfx
        foreach row $dfx(rps) {
            lassign $row id rp
            if {$name eq $rp || [string first "$rp/" $name]==0} {return $id}
        }
        return static
    }
    proc fabric {type} {
        return [regexp {^(LUT[1-6](_2)?|FD(RE|SE|CE|PE)|LDCE|AND2B1L|CARRY8|MUXF[78]|SRL(C32E|16E)|RAM.*|FIFO(18|36)E2|DSP48E2|URAM288.*)$} $type]
    }
    proc export_regions {out} {
        global dfx
        set f [open $dfx(modeled_sites) r];array set modeled {}
        foreach site [split [read $f] \n] {if {$site ne ""} {set modeled($site) 1}};close $f
        array set owner {};array set excluded {};set regionSites [dict create]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set sites {}
            foreach site [get_property NAME [get_sites -of_objects [get_pblocks $pb] -filter {PROHIBIT == 0}]] {
                if {![info exists modeled($site)]} {continue}
                if {[info exists owner($site)]} {error "Overlapping full AMF RP regions at $site"}
                set owner($site) $id;lappend sites $site
            }
            if {![llength $sites]} {error "No modeled sites for region $id"}
            dict set regionSites $id $sites
            if {![amf_static::enabled]} {
                # Only placement ownership excludes static cells. Static wires may
                # use the RP routing footprint; do not reserve whole CRs for them.
                set tiles [get_dfx_footprint -place -of_objects [get_cells $rp]]
                if {![llength $tiles]} {error "Missing RP placement footprint: $rp"}
                foreach site [get_property NAME [get_sites -quiet -of_objects $tiles]] {set excluded($site) 1}
            }
        }
        if {[amf_static::enabled]} {
            set candidates [get_property NAME [get_sites -of_objects [get_pblocks pblock_static] -filter {PROHIBIT == 0}]]
        } else {
            set candidates [get_property NAME [get_sites -quiet -filter {PROHIBIT == 0}]]
        }
        set staticSites {}
        foreach site $candidates {
            if {![info exists modeled($site)]} {continue}
            if {[info exists owner($site)]} {
                if {[amf_static::enabled]} {error "Overlapping full AMF region at $site"}
                continue
            }
            if {[info exists excluded($site)]} {continue}
            lappend staticSites $site
        }
        if {![llength $staticSites]} {error "No modeled sites for static logic"}
        dict set regionSites static $staticSites
        set regions [open $out/hard_regions.tsv w];puts $regions "region\tinstance";puts $regions "static\t*"
        set sf [open $out/hard_region_sites.tsv w];puts $sf "region\tsite"
        set total 0
        foreach row [linsert $dfx(rps) 0 [list static *]] {
            lassign $row id rp
            if {$id ne "static"} {puts $regions "$id\t$rp"}
            foreach site [dict get $regionSites $id] {puts $sf "$id\t$site";incr total}
        }
        close $sf;close $regions
        puts "AMF_FULL_REGION_SITES $total static_containment=[amf_static::enabled]"
        return $total
    }
    proc export {out} {
        global dfx
        export_regions $out
        set all [amf_dfx::primitive_cells];array set selected {};array set infra {};set constants 0
        variable typeOf
        set types [get_property REF_NAME $all];set names [get_property NAME $all]
        foreach c $names t $types {
            set typeOf($c) $t
            if {$t in {GND VCC}} {incr constants;continue}
            if {[fabric $t]} {set selected($c) $t;continue}
            if {[region $c] ne "static" || ![regexp {^(BUFG.*|BSCANE2)$} $t]} {error "Unsupported full-design primitive: $c $t"}
            set infra($c) $t
        }
        set cf [open $out/cells.tsv w];puts $cf "cell\tprimitive\tregion"
        set nf [open $out/netlist.txt w];set clocks [dict create]
        set inf [open $out/infrastructure.tsv w];puts $inf "cell\tprimitive\tplacement_owner"
        foreach c [lsort [array names infra]] {puts $inf "$c\t$infra($c)\tVivado dedicated clock/configuration placement"};close $inf
        set srl [open $out/srl_controls.tsv w];puts $srl "cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks"
        set boundary [open $out/dedicated_interface_pins.tsv w];puts $boundary "fabric_pin\tdedicated_driver"
        set count 0
        foreach c $names {
            if {![info exists selected($c)]} {continue}
            set t $selected($c)
            puts $cf "$c\t$t\t[region $c]";puts $nf "curCell=> $c type=> $t"
            foreach row [amf_dfx_scope::pins $c] {
                lassign $row p ref dir n clk;set driver {}
                if {$n ne ""} {
                    if {$clk} {
                        set driver [amf_dfx_scope::clock_driver $n]
                        if {$dir eq "IN"} {dict set clocks $driver 1}
                    } else {
                        lassign [amf_full::driver $n] driver kind
                        if {$kind eq "GND"} {set driver <const0>} elseif {$kind eq "VCC"} {set driver <const1>} else {
                            set owner [amf_dfx_scope::owner $driver]
                            if {![info exists selected($owner)]} {
                                if {![info exists infra($owner)]} {error "Missing full AMF data driver: $p $driver"}
                                puts $boundary "$p\t$driver"
                            }
                        }
                    }
                }
                puts $nf " pin=> $p refpin=> $ref dir=> $dir net=> $n drivepin=> $driver"
            }
            if {$t in {SRL16E SRLC32E}} {
                set inv [amf_dfx_srl::inversion [get_cells $c] $t]
                puts $srl [join [list $c $t - - $inv [get_property IS_LOC_FIXED [get_cells $c]] [get_property IS_BEL_FIXED [get_cells $c]] -] \t]
            }
            if {$t eq "DSP48E2"} {amf_dfx_scope::terminal $c}
            dict unset ::amf_dfx_scope::pinCache $c
            incr count;if {$count%4096==0} {puts "AMF_FULL_EXPORT cells=$count/[array size selected]"}
        }
        close $cf;close $nf;close $srl;close $boundary
        set f [open $out/clocks w];puts $f [join [lsort [dict keys $clocks]] \n];close $f
        set f [open $out/dsp_registered_outputs.txt w];puts $f "AMF_DSP_REGISTERED_OUTPUTS 1"
        dict for {c data} $::amf_dfx_scope::dspCertificates {if {[lindex $data 2]} {puts $f "$c PREG=1"}};close $f
        set f [open $out/full_input_coverage.tsv w]
        puts $f "canonical_primitives\t[llength $all]";puts $f "amf_fabric_primitives\t[array size selected]"
        puts $f "dedicated_infrastructure\t[array size infra]";puts $f "constants\t$constants";puts $f "static_fabric_reused\t0";close $f
        if {[llength $all] != [array size selected]+[array size infra]+$constants} {error "Full-design inventory mismatch"}
    }
}
