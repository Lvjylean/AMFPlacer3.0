# Packing adaptation applies to the active RM only. Static shell constraints stay intact.
namespace eval amf_dfx {
    proc reset_active_lut_groups {out requested} {
        global dfx
        set before [current_instance]
        current_instance $dfx(active_partition)
        set code [catch {
            set cells [get_cells -quiet -hier -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME =~ LUT*}]
        } value opts]
        current_instance
        if {$before ne ""} {current_instance $before}
        if {$code} {return -options $opts $value}
        # Validate the entire selection before changing any packing property.
        foreach cell $cells {
            set name [get_property NAME $cell]
            if {![active $name] || ![dict exists $requested $name]} {error "Unexpected LUT outside requested active RM: $name"}
        }
        set f [open $out/import_group_reset.tsv w]
        puts $f "cell\tproperty\tprevious_value"
        set count 0
        foreach property {HLUTNM SOFT_HLUTNM} {
            set grouped {}
            foreach cell $cells {
                set name [get_property NAME $cell]
                set group [get_property -quiet $property $cell]
                if {$group eq ""} {continue}
                puts $f "$name\t$property\t$group"
                lappend grouped $cell
                incr count
            }
            if {[llength $grouped]} {set_property $property {} $grouped}
        }
        close $f
        puts "AMF_DFX_LUT_GROUP_RESET properties=$count active_only=1"
    }
}
