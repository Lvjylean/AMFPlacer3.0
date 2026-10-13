# Multi-RP orchestration helpers. Each worker still has exactly one active RP.
source [file join [file dirname [info script]] dfx_common.tcl]
namespace eval amf_multi {
    proc active {name} {
        global dfx
        foreach row $dfx(rps) {
            set rp [lindex $row 1]
            if {$name eq $rp || [string first "$rp/" $name] == 0} {return 1}
        }
        return 0
    }
    proc static_snapshot {} {
        set data [dict create]
        foreach c [amf_dfx::primitive_cells] {
            if {[active $c] || [amf_dfx::constant $c]} {continue}
            set loc [get_property LOC $c]; set bel [get_property BEL $c]
            if {$loc eq "" || $bel eq ""} {error "Unplaced static cell $c"}
            dict set data [get_property NAME $c] [list [get_property REF_NAME $c] $loc $bel]
        }
        return $data
    }
    proc module_snapshot {rp} {
        set data [dict create]
        foreach c [amf_dfx::primitive_cells] {
            if {[string first "$rp/" $c] != 0 || [amf_dfx::constant $c]} {continue}
            dict set data [get_property NAME $c] [list [get_property REF_NAME $c] [get_property LOC $c] [get_property BEL $c]]
        }
        return $data
    }
    proc names {objects} {
        # Vivado object handles turn into null after close_design/read_checkpoint.
        # Materialize property strings before persisting data between designs.
        set result {}
        foreach object $objects {lappend result [get_property NAME $object]}
        return [lsort $result]
    }
    proc module_routing {rp} {
        set data [dict create]
        foreach n [get_nets -hierarchical] {
            if {[string first "$rp/" $n] != 0} {continue}
            dict set data [get_property NAME $n] [list [names [get_nodes -quiet -of_objects $n]] [names [get_pips -quiet -of_objects $n]]]
        }
        return $data
    }
    proc validate_regions {} {
        global dfx
        set owned [dict create]; set slrs {}
        set f [open $dfx(output)/regions.tsv w]
        puts $f "id\tpartition\tpblock\tslrs\tsites\tderived_ranges"
        foreach row $dfx(rps) {
            lassign $row id rp pb ranges expected
            set dfx(active_partition) $rp; set dfx(pblock) $pb
            amf_dfx::assert_context
            if {![get_property EXCLUDE_PLACEMENT [get_pblocks $pb]] || ![get_property CONTAIN_ROUTING [get_pblocks $pb]]} {error "Non-isolated RP $rp"}
            set sites [get_sites -of_objects [get_pblocks $pb]]
            if {![llength $sites]} {error "Empty Pblock $pb"}
            foreach site $sites {
                if {[dict exists $owned $site]} {error "Overlapping Pblocks at $site"}
                dict set owned $site $id
            }
            set actual [lsort -unique [get_slrs -of_objects $sites]]
            if {$expected ne "" && $actual ne $expected} {error "RP $rp expected $expected but occupies $actual"}
            if {$dfx(require_distinct_slrs)} {
                if {[llength $actual]!=1 || [lsearch -exact $slrs $actual]>=0} {error "RPs must occupy distinct individual SLRs"}
                lappend slrs $actual
            }
            puts $f [join [list $id $rp $pb $actual [llength $sites] [get_property DERIVED_RANGES [get_pblocks $pb]]] "\t"]
        }
        close $f
    }
    proc reports {prefix} {
        report_route_status -file ${prefix}_route_status.rpt
        report_timing_summary -delay_type min_max -report_unconstrained -file ${prefix}_timing.rpt
        report_drc -file ${prefix}_drc.rpt
        set f [open ${prefix}_drc_counts.tsv w]
        puts $f "errors\t[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]"
        puts $f "critical_warnings\t[llength [get_drc_violations -quiet -filter {SEVERITY == "Critical Warning"}]]"
        close $f
    }
}
