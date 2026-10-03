# The AMF import must stand on its own, before any Vivado placement repair.
# RAM32X1D occupies both LUTs: Vivado reports its DP/G6LUT as the primary
# BEL even when place_cell was given the SP/H6LUT anchor. Accept only the
# physically verified two-BEL footprint, never a primary-name guess alone.
proc amf3_ram32x1d_anchor_match {type requestedSite requestedBel actualSite actualBel occupiedBels} {
    if {$type ne "RAM32X1D" || $requestedSite ne $actualSite ||
        $requestedBel ne "H6LUT" || $actualBel ne "G6LUT"} {return 0}
    set expected [list ${requestedSite}/G6LUT ${requestedSite}/H6LUT]
    return [expr {[lsort -unique $occupiedBels] eq $expected}]
}
proc require_legal_amf_import {metrics} {
    set required [dict get $metrics requested]
    foreach key {present placed exact_loc_bel_matches exact_original_loc_bel_matches} {
        if {[dict get $metrics $key] != $required} {
            error "AMF strict import failed: $key=[dict get $metrics $key], requested=$required"
        }
    }
    foreach key {rejection_events srl_violations cascade_violations} {
        if {[dict get $metrics $key] != 0} {
            error "AMF strict import failed: $key=[dict get $metrics $key]"
        }
    }
}
