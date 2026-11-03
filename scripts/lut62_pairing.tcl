# Preserve mandatory composite-LUT pairing while replacing ordinary packing hints.
namespace eval amf_lut62 {
    variable pairs [dict create]
    variable protected [dict create]
}

proc amf_lut62::capture {} {
    variable pairs; variable protected
    set pairs [dict create]; set protected [dict create]
    foreach parent [get_cells -quiet -hierarchical -filter {REF_NAME == LUT6_2}] {
        set name [get_property NAME $parent]
        set members [list]
        foreach suffix {LUT5 LUT6} {
            set cell [get_cells -quiet $name/$suffix]
            if {[llength $cell] != 1 || [get_property REF_NAME $cell] ne $suffix} {
                error "LUT6_2 member missing or changed: $name/$suffix"
            }
            set values [list]
            foreach prop {LUTNM HLUTNM SOFT_HLUTNM} {lappend values [get_property $prop $cell]}
            dict set protected [get_property NAME $cell] $values
            lappend members $values
        }
        lassign $members a b
        set grouped 0
        foreach i {0 1} {
            if {[lindex $a $i] ni {{} DISABLED} && [lindex $a $i] eq [lindex $b $i]} {set grouped 1}
        }
        if {!$grouped} {error "LUT6_2 has no valid original pairing: $name"}
        dict set pairs $name $members
    }
}

proc amf_lut62::clear_ordinary_groups {} {
    variable protected
    set cleared 0; set retained 0; set started [clock milliseconds]
    foreach prop {HLUTNM SOFT_HLUTNM} {
        set grouped [get_cells -quiet -hierarchical -filter "$prop != \"\""]
        set ordinary [list]
        # Membership checks are constant time; never traverse connectivity.
        foreach cell $grouped {
            if {[dict exists $protected $cell]} {incr retained} else {lappend ordinary $cell}
        }
        if {[llength $ordinary]} {set_property $prop {} $ordinary}
        incr cleared [llength $ordinary]
    }
    set f [open [file join $::out lut62_group_cleanup.json] w]
    puts $f [format {{"protected_members":%d,"retained_properties":%d,"cleared_ordinary_properties":%d,"elapsed_ms":%d}} [dict size $protected] $retained $cleared [expr {[clock milliseconds]-$started}]]
    close $f
}

proc amf_lut62::check {stage requirePlaced} {
    variable pairs; variable protected
    set started [clock milliseconds]
    set physical 0; set changed 0; set kinds 0
    set f [open [file join $::out ${stage}_lut62.tsv] w]
    puts $f "parent\tlut5_bels\tlut6_bels\tpairing_preserved\tphysical_pair"
    dict for {name original} $pairs {
        set occupied [list]; set same 1
        foreach suffix {LUT5 LUT6} {
            set cell [get_cells -quiet $name/$suffix]
            if {[llength $cell] != 1 || [get_property REF_NAME $cell] ne $suffix} {
                incr kinds; set same 0; lappend occupied {}; continue
            }
            set values [list]
            foreach prop {LUTNM HLUTNM SOFT_HLUTNM} {lappend values [get_property $prop $cell]}
            if {$values ne [dict get $protected $name/$suffix]} {set same 0}
            lappend occupied [get_bels -quiet -of_objects $cell]
        }
        lassign $occupied a b
        set ok 0
        if {[llength $a] == 1 && [llength $b] == 1 &&
            [regexp {^(.+)/([A-H])5LUT$} [lindex $a 0] -> site lane]} {
            set ok [expr {[lindex $b 0] eq "$site/${lane}6LUT"}]
        }
        incr physical $ok
        if {!$same} {incr changed}
        puts $f [join [list $name [join $a ,] [join $b ,] $same $ok] "\t"]
    }
    close $f
    set f [open [file join $::out ${stage}_lut62.json] w]
    puts $f [format {{"pairs":%d,"physical_pairs":%d,"pairing_changes":%d,"member_type_errors":%d,"require_placed":%d,"elapsed_ms":%d}} [dict size $pairs] $physical $changed $kinds $requirePlaced [expr {[clock milliseconds]-$started}]]
    close $f
    if {$changed || $kinds || ($requirePlaced && $physical != [dict size $pairs])} {
        error "LUT6_2 contract failed: pairing_changes=$changed member_type_errors=$kinds physical=$physical/[dict size $pairs]"
    }
}
