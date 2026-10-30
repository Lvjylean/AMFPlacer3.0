# Run-scoped compatibility guard for confirmed post-import replication.
# All normal SLL/cache/DRC/static checks remain enabled.
namespace eval full_topology_guard {
    variable snapshots [dict create]
    proc exact_net {name} {
        set escaped [string map [list "\\" "\\\\" "\"" "\\\""] $name]
        set nets [get_nets -quiet -hierarchical -filter "NAME == \"$escaped\""]
        if {[llength $nets]!=1} {error "Topology guard requires one exact net: $name"}
        return $nets
    }
    proc snapshot {name} {
        set net [exact_net $name]
        set segments [get_nets -quiet -segments $net]
        if {![llength $segments]} {error "Topology guard missing net segments: $name"}
        set outputs [get_pins -quiet -leaf -of_objects $segments -filter {DIRECTION == OUT}]
        set drivers [lsort -unique [get_property NAME $outputs]]
        if {[llength $drivers]!=1} {error "Topology guard ambiguous leaf driver: $name $drivers"}
        set cells [get_cells -quiet -of_objects $outputs]
        if {[llength $cells]!=1 || ![get_property IS_PRIMITIVE $cells]} {
            error "Topology guard requires one primitive driver: $name"
        }
        set inputs [get_pins -quiet -leaf -of_objects $segments -filter {DIRECTION == IN}]
        set sinks [lsort -unique [get_property NAME $inputs]]
        if {![llength $sinks]} {error "Topology guard requires actual receivers: $name"}
        return [dict create name $name segments $segments driver_cells $cells drivers $drivers sinks $sinks]
    }
    proc apply {namesFile out} {
        variable snapshots
        if {[dict size $snapshots]} {error "Topology guard already applied"}
        set f [open $namesFile r];set names {}
        while {[gets $f name]>=0} {if {$name ne ""} {lappend names $name}};close $f
        if {![llength $names] || [llength [lsort -unique $names]]!=[llength $names]} {
            error "Topology guard expects a nonempty unique exact-net list"
        }
        # Resolve and validate the entire request before changing any property.
        set pending [dict create]
        foreach name $names {dict set pending $name [snapshot $name]}
        set f [open $out/topology-guard-properties.tsv w]
        puts $f "net\tobject_kind\tobject\tprevious_dont_touch"
        dict for {name data} $pending {
            foreach {kind key} {net segments driver driver_cells} {
                set objects [dict get $data $key]
                foreach object $objects {
                    puts $f [join [list $name $kind [get_property NAME $object] [get_property DONT_TOUCH $object]] \t]
                }
                set_property DONT_TOUCH true $objects
                foreach value [get_property DONT_TOUCH $objects] {
                    if {![string is boolean -strict $value] || !$value} {close $f;error "Failed topology protection: $name"}
                }
            }
        };close $f
        set snapshots $pending
        set f [open $out/topology-guard-before.tcldata w];puts $f $snapshots;close $f
        puts "FULL_TOPOLOGY_GUARD_APPLIED nets=[dict size $snapshots]"
    }
    proc verify {out} {
        variable snapshots
        if {![dict size $snapshots]} {error "Topology guard was not applied"}
        set f [open $out/topology-guard-check.tsv w]
        puts $f "net\tdrivers_equal\treceivers_equal\treceivers"
        dict for {name before} $snapshots {
            set after [snapshot $name]
            set driversEqual [expr {[dict get $before drivers] eq [dict get $after drivers]}]
            set sinksEqual [expr {[dict get $before sinks] eq [dict get $after sinks]}]
            puts $f [join [list $name $driversEqual $sinksEqual [llength [dict get $after sinks]]] \t]
            if {!$driversEqual || !$sinksEqual} {close $f;error "Protected topology still changed: $name"}
        };close $f
        puts "FULL_TOPOLOGY_GUARD_VERIFIED nets=[dict size $snapshots]"
    }
}
