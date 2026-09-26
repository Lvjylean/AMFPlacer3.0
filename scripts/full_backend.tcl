# Import AMF assignments, repair/complete placement, and route the same design.
if {$argc != 3} { error "Expected input.dcp reports-directory placement-directory" }
lassign $argv input out placement
set_param general.maxThreads 4
set timeline [open [file join $out stages.tsv] w]
puts $timeline "stage\tseconds"
proc timed {name body} {
    global timeline
    puts "AMF_STAGE_START $name"
    set begin [clock milliseconds]
    uplevel 1 $body
    set seconds [expr {([clock milliseconds]-$begin)/1000.0}]
    puts $timeline "$name\t$seconds"; flush $timeline
    puts "AMF_STAGE_FINISH $name $seconds"
}
array set requested {}
set f [open [file join $placement requested.tsv] r]
while {[gets $f line] >= 0} { lassign [split $line "\t"] name target; set requested($name) $target }
close $f
array set originalOverrides {}
set fixes [file join $placement bel_corrections.tsv]
if {[file exists $fixes]} {
    set f [open $fixes r];gets $f header
    while {[gets $f line] >= 0} {lassign [split $line "\t"] name before after;set originalOverrides($name) $before}
    close $f
}
proc audit {stage} {
    global requested originalOverrides out placement
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    set names [get_property NAME $cells]
    set locs [get_property LOC $cells]
    set bels [get_property BEL $cells]
    set placed 0; set present 0; set matched 0;set siteMatched 0;set originalMatched 0
    array set actual {}
    array set actualBel {}
    foreach name $names loc $locs bel $bels {
        if {![info exists requested($name)]} {continue}
        incr present
        set actual($name) $loc
        set bel [lindex [split [file tail $bel] .] end]
        set actualBel($name) $bel
        if {$loc ne ""} {incr placed}
        lassign [split $requested($name) /] site wantedBel
        if {$loc eq $site} {incr siteMatched}
        if {$loc eq $site && ($wantedBel eq "" || $wantedBel eq $bel)} {incr matched}
        if {[info exists originalOverrides($name)]} {lassign [split $originalOverrides($name) /] site wantedBel}
        if {$loc eq $site && ($wantedBel eq "" || $wantedBel eq $bel)} {incr originalMatched}
    }
    set f [open [file join $out ${stage}_placement.json] w]
    puts $f [format {{"requested":%d,"present":%d,"placed":%d,"exact_loc_bel_matches":%d,"site_matches":%d,"exact_original_loc_bel_matches":%d,"design_primitive_cells":%d,"unrequested_primitive_cells":%d}} [array size requested] $present $placed $matched $siteMatched $originalMatched [llength $names] [expr {[llength $names]-$present}]]
    close $f
    puts "AMF_PLACEMENT_AUDIT $stage requested=[array size requested] present=$present placed=$placed matched=$matched"
    if {$present != [array size requested]} { error "Requested cells missing from Vivado design" }
    set srlFile [file join $placement srl_cascades.tsv]
    if {[file exists $srlFile]} {
        set f [open $srlFile r];gets $f header
        set checked 0;set violations 0
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] source sink
            incr checked
            set a $actual($source);set b $actual($sink)
            set ab $actualBel($source);set bb $actualBel($sink)
            if {$a eq "" || $b eq ""} {incr violations;continue}
            if {$ab eq "A6LUT"} {continue}
            if {![regexp {^([B-H])6LUT$} $ab ignored letter]} {incr violations;continue}
            scan $letter %c code
            set expected "[format %c [expr {$code-1}]]6LUT"
            if {$a ne $b || $bb ne $expected} {incr violations}
        }
        close $f
        set f [open [file join $out ${stage}_srl_cascades.json] w]
        puts $f [format {{"checked":%d,"violations":%d}} $checked $violations];close $f
        if {$stage ne "imported" && $violations > 0} {error "SRL cascades lack a legal MC31 path: $violations"}
    }
    set hard [file join $placement resources.tsv]
    if {[file exists $hard]} {
        set f [open $hard r];gets $f header
        set audit [open [file join $out ${stage}_hard_resources.tsv] w]
        puts $audit "cell\ttype\trequested_site\tactual_site\tactual_bel\tactual_slr"
        array set siteSLR {}
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] name type site bel slr
            set loc $actual($name)
            if {$loc eq ""} {set actualSlr ""} else {
                if {![info exists siteSLR($loc)]} {set siteSLR($loc) [get_slrs -quiet -of_objects [get_sites $loc]]}
                set actualSlr $siteSLR($loc)
            }
            puts $audit "$name\t$type\t$site\t$loc\t$actualBel($name)\t$actualSlr"
        }
        close $f;close $audit
        set cascadeFile [file join $placement cascades.tsv]
        if {[file exists $cascadeFile]} {
            set f [open $cascadeFile r];gets $f header
            set checked 0;set violations 0
            while {[gets $f line] >= 0} {
                lassign [split $line "\t"] source sink family
                incr checked
                set a $actual($source);set b $actual($sink)
                if {$a eq "" || $b eq ""} {incr violations;continue}
                regexp {_X([0-9]+)Y([0-9]+)$} $a ignored ax ay
                regexp {_X([0-9]+)Y([0-9]+)$} $b ignored bx by
                if {$siteSLR($a) ne $siteSLR($b) || $ax != $bx || $by != $ay+1} {incr violations}
            }
            close $f
            set f [open [file join $out ${stage}_cascades.json] w]
            puts $f [format {{"checked":%d,"violations":%d}} $checked $violations];close $f
            if {$stage ne "imported" && $violations > 0} {error "Dedicated cascades violate same-SLR adjacency: $violations"}
        }
    }
}
if {[catch {
    timed open {open_checkpoint $input}
    set f [open [file join $out tools.json] w]
    puts $f [format {{"vivado_version":"%s","part":"%s","max_threads":4}} [version -short] [get_property PART [current_design]]]
    close $f
    report_clocks -file [file join $out clocks.rpt]
    timed import {source [file join $placement import_placement.tcl]}
    timed import_audit {audit imported}
    timed place {place_design}
    timed place_audit {audit placed}
    write_checkpoint [file join $out getrf_placed.dcp]
    timed route {route_design}
    timed route_audit {audit routed}
    timed reports {
        report_route_status -file [file join $out route_status.rpt]
        report_drc -file [file join $out drc.rpt]
        report_timing_summary -delay_type min_max -report_unconstrained -file [file join $out timing_summary.rpt]
        report_utilization -file [file join $out utilization.rpt]
        set f [open [file join $out drc_counts.json] w]
        set errors [llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]
        set critical [llength [get_drc_violations -quiet -filter {SEVERITY == "Critical Warning"}]]
        puts $f [format {{"errors":%d,"critical_warnings":%d}} $errors $critical];close $f
    }
    timed checkpoint {write_checkpoint [file join $out getrf_routed.dcp]}
    puts "AMF_FULL_FLOW_FINISHED"
} failure options]} {
    puts stderr "AMF_FULL_FLOW_FAILED: $failure"
    puts stderr [dict get $options -errorinfo]
    close $timeline
    exit 2
}
close $timeline
exit 0
