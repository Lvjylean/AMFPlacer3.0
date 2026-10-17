# Compare interface snapshots without treating an initially empty PPLOC as fixed.
# This local check supplements, and never replaces, routed pr_verify -full_check.
namespace eval dfx_boundary {
    proc compare {before after} {
        if {[lsort [dict keys $before]] ne [lsort [dict keys $after]]} {
            error "Partition interface pin set changed"
        }
        set preserved 0; set remaining 0; set assigned [dict create]
        dict for {pin old} $before {
            set new [dict get $after $pin]
            if {[llength $old] != 2 || [llength $new] != 2} {error "Malformed boundary entry: $pin"}
            lassign $old olddir oldloc
            lassign $new newdir newloc
            if {$olddir ne $newdir} {error "Partition interface direction changed: $pin"}
            if {$oldloc ne ""} {
                if {$oldloc ne $newloc} {error "Assigned partition anchor changed: $pin"}
                incr preserved
            } elseif {$newloc ne ""} {
                dict set assigned $pin $newloc
            } else {
                incr remaining
            }
        }
        return [dict create assigned_preserved $preserved newly_assigned $assigned \
            unassigned_remaining $remaining snapshots_equal [expr {[dict size $assigned] == 0}]]
    }
}
