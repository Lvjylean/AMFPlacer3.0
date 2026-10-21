# Produce the common static input once. Retain RP shells and static interfaces,
# but retain no implementation of any old RM. No placement or routing is run.
namespace eval amf_common_static {
    proc boundaries {} {
        global dfx
        set result {}
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set dfx(active_partition) $rp;set dfx(pblock) $pb
            amf_dfx::assert_context
            dict set result $id [amf_dfx::boundary_snapshot]
        }
        return $result
    }
    proc assert_empty {label expected} {
        global dfx
        amf_multi::validate_regions
        set f [open $dfx(output)/common_static_emptiness.tsv a]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set dfx(active_partition) $rp;set dfx(pblock) $pb
            amf_dfx::assert_context
            set cell [get_cells -quiet $rp]
            if {![get_property IS_BLACKBOX $cell]} {error "Static input retains an implemented RM: $rp"}
            set previous [current_instance]
            current_instance $rp
            set code [catch {set children [get_cells -quiet -hierarchical]} message options]
            current_instance
            if {$previous ne ""} {current_instance $previous}
            if {$code} {return -options $options $message}
            if {[llength $children]} {error "Static input retains RM descendants: $rp"}
            set actual [amf_dfx::boundary_snapshot]
            if {![dict size $actual]} {error "Missing RP interface in static input: $rp"}
            dfx_boundary::compare [dict get $expected $id] $actual
            puts $f [join [list $label $id $rp 1 0 [dict size $actual]] \t]
        }
        close $f
    }
    proc create {reference out} {
        global dfx
        amf_dfx::timed common_static_open_reference {open_checkpoint $reference}
        amf_multi::validate_regions
        set before [boundaries]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            amf_dfx::save_dict $out/[set id]_common_static_boundary.tcldata [dict get $before $id]
        }
        amf_dfx::timed common_static_remove_all_rms {
            foreach row $dfx(rps) {lassign $row id rp pb;update_design -cell $rp -black_box}
        }
        amf_dfx::timed common_static_lock_routing {lock_design -level routing}
        amf_dfx::timed common_static_validate_empty {assert_empty locked $before}
        report_route_status -file $out/common_static_route_status.rpt
        amf_dfx::timed common_static_write {write_checkpoint $out/common_static_locked.dcp}
        close_design
        # A writer success alone does not certify the artifact that consumers open.
        amf_dfx::timed common_static_reopen {open_checkpoint $out/common_static_locked.dcp}
        amf_dfx::timed common_static_validate_reopened {assert_empty reopened $before}
        if {[info exists dfx(native_pin_access_automatic)] && $dfx(native_pin_access_automatic)} {
            source [file join [file dirname [info script]] dfx_native_pin_access.tcl]
            amf_dfx::timed native_pin_access_collection {amf_pin_access::collect}
        }
        close_design
    }
}
