# Keep LUT/MUX/site dependencies together, as in the AMF packing export.
# No position changes, fallback placement, or error suppression are allowed here.
namespace eval amf_site_import {}
proc amf_site_import::place {requestedList {batchCells 256}} {
    if {$batchCells < 1} {error "Invalid import batch size"}
    array set requested $requestedList
    array set groups {}
    foreach cell [lsort [array names requested]] {
        set target $requested($cell)
        if {![regexp {^([^/]+)(/[^/]+)?$} $target -> site]} {
            error "Invalid placement target for $cell: $target"
        }
        lappend groups($site) $cell $target
    }
    set batch {};set count 0;set calls 0
    foreach site [lsort -dictionary [array names groups]] {
        set group $groups($site)
        if {[llength $batch] && [llength $batch]+[llength $group]>2*$batchCells} {
            place_cell $batch;incr calls;set batch {}
        }
        lappend batch {*}$group
        incr count [expr {[llength $group]/2}]
    }
    if {[llength $batch]} {place_cell $batch;incr calls}
    puts "AMF_SITE_ATOMIC_IMPORT cells=$count sites=[array size groups] calls=$calls"
    return [list cells $count sites [array size groups] calls $calls]
}
