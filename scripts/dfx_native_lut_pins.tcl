# Adapter for AMF-selected logical-to-physical LUT pin assignments. Selection
# belongs to AMF; Vivado only applies and verifies the exact emitted table.
namespace eval amf_native_lut_pins {
    proc verify {path prefix root label} {
        set plan [read_plan $path $prefix]
        set seen {}
        if {$root eq ""} {
            set cells [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && REF_NAME =~ LUT*}]
        } else {
            set before [current_instance];current_instance $root
            set cells [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && REF_NAME =~ LUT*}]
            current_instance
            if {$before ne ""} {current_instance $before}
        }
        foreach c $cells {
            set full [get_property NAME $c]
            set name $full
            if {$root ne ""} {
                if {[string first "$root/" $full]!=0} {error "LUT coverage escaped RP"}
                set name [string range $full [expr {[string length $root]+1}] end]
            }
            if {![dict exists $plan $name]} {error "LUT missing pin plan at $label: $full"}
            dict set seen $name 1
        }
        if {[lsort [dict keys $seen]] ne [lsort [dict keys $plan]]} {error "Extra/missing LUT cells at $label"}
        dict for {name spec} $plan {
            set full $name;if {$root ne ""} {set full "$root/$name"}
            set c [get_cells -quiet [list $full]]
            if {[llength $c]!=1} {error "Missing LUT at $label: $full"}
            set actual [list [get_property LOC $c] [lindex [split [get_property BEL $c] .] end]]
            if {$actual ne [dict get $spec target]} {error "LUT LOC/BEL changed at $label: $full"}
            set expected {}
            dict for {logical physical} [dict get $spec pins] {
                lappend expected "$logical:$physical"
                set p [get_pins -of_objects $c -filter "REF_PIN_NAME == $logical"]
                if {[llength $p]!=1} {error "Missing LUT logical pin"}
                set physicalPins {}
                foreach bp [get_bel_pins -of_objects $p] {
                    lappend physicalPins [lindex [split [get_property NAME $bp] /] end]
                }
                if {[lsort -unique $physicalPins] ne [list $physical]} {error "Physical LUT pin differs at $label: $full/$logical expected=$physical actual=$physicalPins"}
            }
            if {[lsort [get_property LOCK_PINS $c]] ne [lsort $expected]} {error "LUT LOCK_PINS changed at $label: $full"}
        }
        puts "AMF_LUT_PIN_READBACK stage=$label cells=[dict size $plan]"
        return [dict size $plan]
    }
    proc read_plan {path prefix} {
        set f [open $path r]
        if {[gets $f] ne "cell\tsite\tbel\tlogical_pin\tphysical_pin"} {error "Invalid AMF LUT pin table"}
        set plan {}
        while {[gets $f line]>=0} {
            if {[llength [split $line \t]]!=5} {error "Malformed LUT pin row"}
            lassign [split $line \t] full site bel logical physical
            if {[string first $prefix $full]!=0} {error "LUT pin assignment outside active RP"}
            if {![regexp {^[A-H][56]LUT$} $bel] || ![regexp {^I[0-5]$} $logical] || ![regexp {^A[1-6]$} $physical]} {error "Invalid LUT BEL/pin"}
            if {[string index $bel 1] eq "5" && $physical eq "A6"} {error "A6 is unavailable on 5LUT"}
            set local [string range $full [string length $prefix] end]
            if {[dict exists $plan $local pins $logical]} {error "Duplicate LUT logical pin"}
            if {[dict exists $plan $local target] && [dict get $plan $local target] ne [list $site $bel]} {error "Inconsistent LUT pin target"}
            if {[dict exists $plan $local pins] && $physical in [dict values [dict get $plan $local pins]]} {error "LUT logical inputs must have distinct physical inputs"}
            dict set plan $local target [list $site $bel]
            dict set plan $local pins $logical $physical
        }
        close $f;return $plan
    }
    proc apply {path prefix} {
        set plan [read_plan $path $prefix]
        dict for {name spec} $plan {
            set c [get_cells -quiet [list $name]]
            if {[llength $c]!=1 || ![regexp {^LUT[1-6](_2)?$} [get_property REF_NAME $c]]} {error "Pin plan does not name a LUT: $name"}
            set actual [list [get_property LOC $c] [lindex [split [get_property BEL $c] .] end]]
            if {$actual ne [dict get $spec target]} {error "LUT target changed before pin locking: $name"}
            set pins [dict get $spec pins];set pairs {}
            foreach p [get_pins -of_objects $c -filter {DIRECTION == IN}] {
                set logical [get_property REF_PIN_NAME $p]
                if {[regexp {^I[0-5]$} $logical] && ![dict exists $pins $logical]} {error "Incomplete AMF LUT pin mapping: $name/$logical"}
            }
            dict for {logical physical} $pins {lappend pairs "$logical:$physical"}
            set_property LOCK_PINS $pairs $c
            if {[lsort [get_property LOCK_PINS $c]] ne [lsort $pairs]} {error "LUT LOCK_PINS differs from AMF plan: $name"}
        }
        puts "AMF_LUT_PIN_MAPPING_EXACT cells=[dict size $plan]"
        return [dict size $plan]
    }
}
