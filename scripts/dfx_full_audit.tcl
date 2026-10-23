# Import coverage is exact. Vivado optimization may add/remove primitives;
# audit every resulting fabric primitive against the same hard region mask.
proc full_region_audit {requested label} {
    global dfx
    set out $dfx(output);array set sites {};array set targets $requested
    array set present {};unset requested
    set f [open $out/hard_region_sites.tsv r];gets $f
    while {[gets $f line]>=0} {lassign [split $line \t] id site;set sites($site) $id};close $f
    set records {};set addedRecords {};set removedRecords {}
    set retained 0;set actual 0;set added 0;set originalPresent 0
    set cells [amf_dfx::primitive_cells]
    set names [get_property NAME $cells];set types [get_property REF_NAME $cells]
    set locs [get_property LOC $cells];set bels [get_property BEL $cells]
    foreach name $names type $types loc $locs rawBel $bels {
        if {![amf_full::fabric $type]} {continue}
        incr actual
        set bel [lindex [split $rawBel .] end]
        set region [amf_full::region $name]
        if {![info exists sites($loc)] || $sites($loc) ne $region} {
            error "Hard DFX region violation after $label: $name $loc $region"
        }
        if {[info exists targets($name)]} {
            if {[info exists present($name)]} {error "Duplicate fabric primitive: $name"}
            set present($name) 1;incr originalPresent
            set target $targets($name);set actualTarget $loc
            if {[string first / $target]>=0} {set actualTarget "$loc/$bel"}
            set equal [expr {$actualTarget eq $target}];incr retained $equal
            lappend records [list $name $target $loc $bel $region $equal original $type]
        } else {
            incr added
            lappend addedRecords [list $name $type $loc $bel $region]
            lappend records [list $name - $loc $bel $region - vivado_added $type]
        }
    }
    foreach name [lsort [array names targets]] {
        if {![info exists present($name)]} {
            lappend removedRecords [list $name $targets($name) [amf_full::region $name]]
        }
    }
    set removed [llength $removedRecords]
    foreach {suffix header rows} [list retention {cell requested loc bel region retained origin primitive} $records \
          added {cell primitive loc bel region} $addedRecords removed {cell requested region} $removedRecords] {
        set f [open $out/${label}_${suffix}.tsv w];puts $f [join $header \t]
        foreach row $rows {puts $f [join $row \t]};close $f
    }
    set summary [list expected_original [array size targets] original_present $originalPresent \
        vivado_added $added removed_original $removed actual_fabric $actual retained_original $retained]
    set f [open $out/${label}_coverage.tsv w]
    foreach {key value} $summary {puts $f "$key\t$value"};close $f
    # Matching the total count alone cannot detect one lost plus one new cell.
    if {$label eq "imported" && ($added || $removed || $actual != [array size targets])} {
        error "Full AMF import coverage changed: added=$added removed=$removed"
    }
    puts "AMF_FULL_REGION_AUDIT stage=$label total=$actual original_present=$originalPresent added=$added removed=$removed retained=$retained"
    return $summary
}
