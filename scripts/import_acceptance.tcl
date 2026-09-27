# The AMF import must stand on its own, before any Vivado placement repair.
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
