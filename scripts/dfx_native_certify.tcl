# One-time final full-design certification. Incremental workers never call this.
source [file join [file dirname [info script]] dfx_native_export.tcl]
namespace eval amf_native_certify {
    # In a full design, asking get_ports for every segment of a large clock
    # traverses a very large collection. Index the complete top-level input-port
    # set once by the same top-net query used below. This preserves top-level
    # drivers without expanding clock sinks or inferring aliases from TYPE.
    proc index_input_ports {} {
        variable inputPorts; array unset inputPorts
        variable drivers; array unset drivers
        foreach p [get_ports -quiet -filter {DIRECTION == IN}] {
            set n [get_nets -quiet -top_net_of_hierarchical_group -of_objects $p]
            if {[llength $n]>1} {error "Ambiguous top-level input port net: $p"}
            if {[llength $n]} {lappend inputPorts([get_property NAME $n]) @PORT/[get_property NAME $p]}
        }
    }
    proc driver {n} {
        variable inputPorts; variable drivers
        if {$n eq ""} {return {}}
        set name [get_property NAME $n]
        if {[info exists drivers($name)]} {return $drivers($name)}
        set type [get_property TYPE $n]
        if {$type eq "GROUND"} {set d <const0>} elseif {$type eq "POWER"} {set d <const1>} else {
            set d [amf_dfx_scope::canonical_pins $n OUT]
            if {[info exists inputPorts($name)]} {set d [concat $d $inputPorts($name)]}
            set d [lsort -unique $d]
            if {[llength $d]!=1} {error "Native certification requires one represented driver: $name $d"}
            set d [lindex $d 0]
        }
        set drivers($name) $d
        return $d
    }
    proc rows {path header} {
        set f [open $path r];set got [gets $f]
        if {$got ne $header} {close $f;error "Native certification header mismatch: $path"}
        set rows {};while {[gets $f line]>=0} {if {$line eq ""} {error "Blank native certification row"};lappend rows $line};close $f
        return $rows
    }
    # Once per accepted full design. Raw physical properties only; C++ owns
    # site selection, margin derivation and incremental candidate masks.
    proc resources {out} {
        global dfx
        set f [open $out/resources.tsv w]
        puts $f "region\tsite\ttype\tclock_region\tprohibit"
        set g [open $out/resource_regions.tsv w];puts $g "region\tclock_regions"
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set block [get_pblocks $pb]
            if {[llength $block]!=1} {error "Missing native resource Pblock: $pb"}
            puts $g "$id\t[get_property GRID_RANGES $block]"
            foreach site [get_sites -of_objects $block] {
                set cr [get_clock_regions -quiet -of_objects $site]
                if {[llength $cr]!=1} {error "Ambiguous native site clock region: $site"}
                puts $f [join [list $id [get_property NAME $site] [get_property SITE_TYPE $site] [get_property NAME $cr] [get_property PROHIBIT $site]] "\t"]
            }
        }
        close $f;close $g
    }
    proc run {root} {
        global dfx
        variable sinkCache;array unset sinkCache
        set out $root/native_cache/certified
        if {[file exists $out]} {error "Refusing to overwrite native certification"}
        file mkdir $out
        amf_native_export::full_interfaces $out
        amf_native_export::partition_nodes $out
        if {[info exists dfx(native_resource_inventory)] && $dfx(native_resource_inventory)} {
            amf_dfx::timed native_resource_inventory {resources $out}
        }
        index_input_ports
        foreach {name header} [list native_ports.tsv "module\tport\tdirection\tdriver\tclock" native_boundary_pins.tsv "pin\tmodule\tport"] {
            set before [rows $root/$name $header];set after [rows $out/$name $header]
            if {[lsort $before] ne [lsort $after]} {error "Full backend changed native interface: $name"}
        }
        set driverTokens [dict create];set pinTokens [dict create];set rpSinks [dict create]
        foreach row [rows $out/native_ports.tsv "module\tport\tdirection\tdriver\tclock"] {
            lassign [split $row \t] id port dir driver clock
            if {$dir eq "OUT" && $driver ne "-"} {dict set driverTokens $driver @RM/$id/$port}
            if {$dir eq "IN" && $driver ne "-"} {dict lappend rpSinks $driver @RM/$id/$port}
        }
        foreach row [rows $out/native_boundary_pins.tsv "pin\tmodule\tport"] {
            lassign [split $row \t] pin id port;dict set pinTokens $pin @RM/$id/$port
        }
        set f [open $root/native_cache/context-spec.tsv r]
        if {[gets $f] ne "cell\tprimitive\tpin\tdirection\tdriver\tsinks"} {error "Native context specification header"}
        set seen [dict create];set count 0;set outputCount 0
        set progress [open $out/progress.tsv w]
        puts $progress "pins\toutput_fanouts\telapsed_seconds\tlast_pin";flush $progress
        set started [clock milliseconds]
        while {[gets $f line]>=0} {
            lassign [split $line \t] cell type pin dir expected expectedSinks
            if {[dict exists $seen $pin]} {error "Duplicate native context pin"};dict set seen $pin 1
            set c [get_cells -quiet [list $cell]];set p [get_pins -quiet [list $pin]]
            if {[llength $c]!=1 || [llength $p]!=1 || [get_property REF_NAME $c] ne $type || [get_property DIRECTION $p] ne $dir} {
                error "Full backend changed cached static cell/pin: $pin"
            }
            set n [get_nets -quiet -top_net_of_hierarchical_group -of_objects $p]
            if {[llength $n]>1} {error "Multiple native context nets: $pin"}
            set driver [driver $n]
            if {$driver ne "" && [amf_native_export::is_clock $p $n]} {set driver @DFX_CLOCK/$driver}
            if {[dict exists $pinTokens $pin]} {set driver [dict get $pinTokens $pin]} elseif {[dict exists $driverTokens $driver]} {set driver [dict get $driverTokens $driver]}
            if {$driver eq ""} {set driver -}
            if {$driver ne $expected} {error "Full backend changed static driver: $pin expected=$expected actual=$driver"}
            if {$dir eq "OUT"} {
                set sinks {}
                if {$n ne ""} {
                    if {![info exists sinkCache($n)]} {
                        set names {}
                        foreach s [amf_dfx_scope::canonical_pins $n IN] {
                            if {[amf_native_export::under_rp $s]} {continue}
                            if {[amf_native_export::fabric [amf_dfx_scope::type [amf_dfx_scope::owner $s]]]} {lappend names $s}
                        }
                        set sinkCache($n) $names
                    }
                    set sinks $sinkCache($n)
                    if {[dict exists $rpSinks $pin]} {set sinks [concat $sinks [dict get $rpSinks $pin]]}
                }
                if {$expectedSinks eq "-"} {set expectedSinks {}}
                if {[lsort -unique $sinks] ne [lsort $expectedSinks]} {error "Full backend changed static fanout: $pin"}
                incr outputCount
            }
            incr count
            if {$count<=8 || $count%256==0} {
                puts $progress "$count\t$outputCount\t[expr {([clock milliseconds]-$started)/1000.0}]\t$pin"
                flush $progress
            }
        };close $f;close $progress
        if {!$count} {error "Empty native context audit"}
        set f [open $out/verification.tsv w]
        foreach {key value} [list static_context_connectivity_exact 1 interfaces_exact 1 pins_checked $count output_fanouts_checked $outputCount] {puts $f "$key\t$value"}
        close $f
        puts "AMF_NATIVE_STATIC_CONTEXT_CERTIFIED pins=$count output_fanouts=$outputCount"
    }
}
