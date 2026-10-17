# Reconcile only missing static membership after netlist-changing commands.
# Geometry, clock constraints, placement, and RP ownership are never relaxed.
namespace eval amf_static_membership {
    variable helper [file join [file dirname [info script]] dfx_static_membership_names.py]
    proc ancestor_owned {name hierarchyMembers cacheName} {
        upvar 1 $cacheName cache
        set trail {}; set current $name; set owned 0
        while {$current ne ""} {
            if {[dict exists $cache $current]} {set owned [dict get $cache $current]; break}
            lappend trail $current
            if {[dict exists $hierarchyMembers $current]} {set owned 1; break}
            set slash [string last / $current]
            if {$slash < 0} {set current ""} else {set current [string range $current 0 [expr {$slash-1}]]}
        }
        foreach item $trail {dict set cache $item $owned}
        return $owned
    }
    proc classify {names direct hierarchyMembers rpPrefixes} {
        set missing {}; set unexpected {}; set cache [dict create]; set staticCount 0
        foreach name $names {
            set dynamic 0
            foreach prefix $rpPrefixes {
                if {$name eq $prefix || [string first "$prefix/" $name] == 0} {set dynamic 1; break}
            }
            set owned [dict exists $direct $name]
            if {!$owned} {
                set slash [string last / $name]
                if {$slash >= 0} {set owned [ancestor_owned [string range $name 0 [expr {$slash-1}]] $hierarchyMembers cache]}
            }
            if {$dynamic} {
                if {$owned} {lappend unexpected $name}
            } else {
                incr staticCount
                if {!$owned} {lappend missing $name}
            }
        }
        return [dict create static_count $staticCount missing $missing unexpected $unexpected]
    }
    proc enforce {label {repair 0}} {
        global dfx
        set start [clock milliseconds]
        if {![amf_static::enabled]} {
            set summary [dict create enabled 0 state not_applicable static_pblock none]
            set f [open $dfx(output)/${label}_static_membership_summary.tsv w]
            dict for {key value} $summary {puts $f "$key\t$value"}; close $f
            puts "AMF_STATIC_MEMBERSHIP stage=$label skipped: no static Pblock"
            return $summary
        }
        set pb [get_pblocks pblock_static]
        if {[llength $pb] != 1 || ![get_property CONTAIN_ROUTING $pb] || [get_property IS_SOFT $pb]} {
            error "Invalid static containment for membership audit"
        }
        set cells [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME != GND && REF_NAME != VCC}]
        set names [get_property NAME $cells]
        unset cells
        set members [get_cells -quiet -of_objects $pb]
        set direct [dict create]; set hierarchyMembers [dict create]
        foreach name [get_property NAME $members] primitive [get_property IS_PRIMITIVE $members] {
            dict set direct $name 1
            if {!$primitive} {dict set hierarchyMembers $name 1}
        }
        unset members
        set prefixes {}; foreach row $dfx(rps) {lappend prefixes [lindex $row 1]}
        # Keep the large pure-name loop outside the embedded Vivado interpreter.
        # Bulk API semantics and exceptional-cell checks remain unchanged.
        variable helper
        set prefix $dfx(output)/${label}_member_names
        foreach {tag values} [list primitives $names direct [dict keys $direct] hierarchy [dict keys $hierarchyMembers] rps $prefixes] {
            set f [open ${prefix}_${tag}.txt w]; puts $f [join $values \n]; close $f
        }
        exec /usr/bin/python3 -I $helper $prefix
        set result [dict create static_count 0 missing {} unexpected {}]
        foreach tag {missing unexpected} {
            set f [open ${prefix}_${tag}.txt r]
            dict set result $tag [split [string trimright [read $f] \n] \n]; close $f
        }
        set f [open ${prefix}_static_count.txt r];dict set result static_count [string trim [read $f]];close $f
        unset names direct hierarchyMembers
        set missing [dict get $result missing]; set unexpected [dict get $result unexpected]
        set f [open $dfx(output)/${label}_static_membership.tsv w]
        puts $f "cell\tissue"
        foreach name $missing {puts $f "$name\tmissing_static_membership"}
        foreach name $unexpected {puts $f "$name\tdynamic_in_static_pblock"}
        close $f
        if {[llength $unexpected]} {error "Static Pblock incorrectly contains RP primitives: [lrange $unexpected 0 9]"}
        if {[llength $missing] && !$repair} {error "Static Pblock misses [llength $missing] primitives after $label"}
        set targets {}
        # Only exceptional cells require scalar API queries. Validate every
        # missing cell before applying a single batch membership mutation.
        foreach name $missing {
            set c [get_cells -quiet [list $name]]
            if {[llength $c] != 1 || [get_property NAME $c] ne $name} {error "Ambiguous missing static primitive: $name"}
            if {[llength [get_pblocks -quiet -of_objects $c]]} {error "Refusing to transfer a cell from another Pblock: $name"}
            lappend targets $c
        }
        if {[llength $targets]} {
            foreach property {NAME LOC BEL IS_LOC_FIXED IS_BEL_FIXED} {set before($property) [get_property $property $targets]}
            set ranges [get_property GRID_RANGES $pb]
            add_cells_to_pblock $pb $targets
            foreach property {NAME LOC BEL IS_LOC_FIXED IS_BEL_FIXED} {
                if {$before($property) ne [get_property $property $targets]} {error "Membership repair changed $property"}
            }
            if {$ranges ne [get_property GRID_RANGES $pb]} {error "Membership repair changed geometry"}
            foreach c $targets {
                if {[get_property NAME [get_pblocks -quiet -of_objects $c]] ne "pblock_static"} {error "Static membership repair failed: $c"}
            }
        }
        set summary [dict create static_primitives [dict get $result static_count] missing_before [llength $missing] \
            repaired [llength $targets] missing_after 0 dynamic_in_static 0 \
            seconds [expr {([clock milliseconds]-$start)/1000.0}]]
        set f [open $dfx(output)/${label}_static_membership_summary.tsv w]
        dict for {key value} $summary {puts $f "$key\t$value"}; close $f
        puts "AMF_STATIC_MEMBERSHIP stage=$label $summary"
        return $summary
    }
}
