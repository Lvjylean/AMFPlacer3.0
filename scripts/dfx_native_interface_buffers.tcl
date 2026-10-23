# Execute AMF's explicit shared-output normalization plan. No topology is
# inferred here and no placement/optimization command is issued.
namespace eval amf_native_buffers {
    proc apply {path module} {
        set f [open $path r]
        if {[gets $f] ne "module\tport\tcell\tinput_driver\toutput_net\tinit"} {error "Invalid AMF interface buffer plan"}
        set plan {};set seen [dict create];set inputs [dict create]
        namespace eval ::amf_native_export {array unset driverCache}
        while {[gets $f line]>=0} {
            set fields [split $line \t]
            if {[llength $fields]!=6} {error "Invalid AMF buffer row"}
            lassign $fields id port cell driver net init
            if {$id ne $module || $init ne "2'h2" || [dict exists $seen $port]} {error "AMF buffer identity/init mismatch"}
            set p [get_ports -quiet [list $port]]
            if {[llength $p]!=1 || [get_property DIRECTION $p] ne "OUT"} {error "Missing AMF output port: $port"}
            set old [get_nets -quiet -of_objects $p]
            if {[llength $old]!=1 || [amf_native_export::driver $old] ne $driver} {error "Changed output driver: $port"}
            if {[llength [get_cells -quiet [list $cell]]] || [llength [get_nets -quiet [list $net]]]} {error "AMF buffer name collision: $cell $net"}
            if {![dict exists $inputs $driver]} {
                dict set inputs $driver [list $old [lsort [amf_dfx_scope::canonical_pins $old IN]]]
            }
            dict set seen $port 1
            lappend plan [list $port $cell $driver $net $old]
        };close $f
        # The entire plan is checked before modifying the private OOC design.
        foreach row $plan {
            lassign $row port cell driver net old
            create_cell -reference LUT1 $cell
            set_property INIT 2'h2 [get_cells [list $cell]]
            create_net $net
            disconnect_net -net $old -objects [get_ports [list $port]]
            connect_net -net $old -objects [get_pins [list $cell/I0]]
            connect_net -net [get_nets [list $net]] -objects [concat [get_pins [list $cell/O]] [get_ports [list $port]]]
        }
        namespace eval ::amf_native_export {array unset driverCache}
        foreach row $plan {
            lassign $row port cell driver net old
            if {[get_property INIT [get_cells [list $cell]]] ne "2'h2" ||
                [amf_native_export::driver [get_nets -of_objects [get_pins [list $cell/I0]]]] ne $driver ||
                [amf_native_export::driver [get_nets -of_objects [get_ports [list $port]]]] ne "$cell/O"} {
                error "Interface buffer equivalence/connection mismatch: $port"
            }
        }
        dict for {driver original} $inputs {
            lassign $original old pins
            set expected $pins
            foreach row $plan {
                lassign $row port cell source net prior
                if {$source eq $driver} {lappend expected $cell/I0}
            }
            if {[lsort [amf_dfx_scope::canonical_pins $old IN]] ne [lsort $expected]} {error "Existing internal fanout changed: $driver"}
        }
        return [llength $plan]
    }
}
