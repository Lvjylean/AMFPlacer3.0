# Import AMF assignments, validate them, then optimize placement and route.
if {$argc < 3 || $argc > 5} { error "Expected input.dcp reports-directory placement-directory ?strict|repair? ?full|import-only?" }
lassign $argv input out placement importPolicy backendMode
if {$backendMode eq ""} {set backendMode full}
if {$importPolicy eq ""} {set importPolicy [expr {$backendMode eq "import-only" ? "strict" : "repair"}]}
if {$importPolicy ni {strict repair} || $backendMode ni {full import-only}} {error "Invalid backend policy/mode"}
if {$backendMode eq "import-only" && $importPolicy ne "strict"} {error "Import-only requires strict acceptance"}
source [file join [file dirname [info script]] import_acceptance.tcl]
set amf3_import_error_events 0
set_param general.maxThreads 4
set timeline [open [file join $out stages.tsv] w]
puts $timeline "stage\tseconds"
proc timed {name body} {
    global timeline
    puts "AMF_STAGE_START $name"; flush stdout
    set begin [clock milliseconds]
    uplevel 1 $body
    set seconds [expr {([clock milliseconds]-$begin)/1000.0}]
    puts $timeline "$name\t$seconds"; flush $timeline
    puts "AMF_STAGE_FINISH $name $seconds"; flush stdout
}
set physicalAudit [file isdirectory [file join $out physical]]
array set requested {}
array set releasedClockBuffers {}
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
    global requested originalOverrides out placement physicalAudit importMetrics amf3_import_error_events releasedClockBuffers
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    set names [get_property NAME $cells]
    set locs [get_property LOC $cells]
    set bels [get_property BEL $cells]
    if {$physicalAudit} {
        set sf [open [file join $placement ${stage}_cell_sites.tsv] w]
        puts $sf "cell\tsite"
        foreach name $names loc $locs {puts $sf "$name\t$loc"}
        close $sf
    }
    set placed 0; set present 0; set matched 0;set siteMatched 0;set originalMatched 0
    set primaryMatched 0;set compositeAliases 0
    set unexpectedMoves 0
    array set actual {}
    array set actualBel {}
    set mismatches [open [file join $out ${stage}_placement_mismatches.tsv] w]
    puts $mismatches "cell\trequested\tactual_site\tactual_bel"
    set aliases [open [file join $out ${stage}_composite_bel_aliases.tsv] w]
    puts $aliases "cell\trequested\tactual_primary_bel\tverified_occupied_bels"
    foreach cell $cells name $names loc $locs bel $bels {
        if {![info exists requested($name)]} {continue}
        incr present
        set actual($name) $loc
        set bel [lindex [split [file tail $bel] .] end]
        set actualBel($name) $bel
        if {$loc ne ""} {incr placed}
        lassign [split $requested($name) /] site wantedBel
        if {$loc eq $site} {incr siteMatched}
        set primaryMatch [expr {$loc eq $site && ($wantedBel eq "" || $wantedBel eq $bel)}]
        set aliasMatch 0
        if {$primaryMatch} {incr primaryMatched} elseif {$loc eq $site && $wantedBel eq "H6LUT" && $bel eq "G6LUT"} {
            set kind [get_property REF_NAME $cell]
            if {$kind eq "RAM32X1D"} {
                set occupied [get_bels -quiet -of_objects $cell]
                set aliasMatch [amf3_ram32x1d_anchor_match $kind $site $wantedBel $loc $bel $occupied]
                if {$aliasMatch} {
                    incr compositeAliases
                    puts $aliases "$name\t$requested($name)\t$loc/$bel\t[join [lsort $occupied] ,]"
                }
            }
        }
        if {$primaryMatch || $aliasMatch} {incr matched}
        if {!$primaryMatch && !$aliasMatch} {
            puts $mismatches "$name\t$requested($name)\t$loc\t$bel"
            if {![info exists releasedClockBuffers($name)]} {incr unexpectedMoves}
        }
        if {[info exists originalOverrides($name)]} {lassign [split $originalOverrides($name) /] site wantedBel}
        if {$loc eq $site && ($wantedBel eq "" || $wantedBel eq $bel || ($aliasMatch && $wantedBel eq "H6LUT"))} {incr originalMatched}
    }
    close $mismatches;close $aliases
    if {$stage ne "imported" && [array size releasedClockBuffers] && $unexpectedMoves} {
        error "Clock-only relocation policy moved $unexpectedMoves other AMF assignments"
    }
    set importMetrics [dict create requested [array size requested] present $present placed $placed exact_loc_bel_matches $matched exact_original_loc_bel_matches $originalMatched primary_bel_matches $primaryMatched composite_bel_alias_matches $compositeAliases rejection_events $amf3_import_error_events srl_violations 0 cascade_violations 0]
    set f [open [file join $out ${stage}_placement.json] w]
    puts $f [format {{"requested":%d,"present":%d,"placed":%d,"exact_loc_bel_matches":%d,"site_matches":%d,"exact_original_loc_bel_matches":%d,"primary_bel_matches":%d,"composite_bel_alias_matches":%d,"design_primitive_cells":%d,"unrequested_primitive_cells":%d}} [array size requested] $present $placed $matched $siteMatched $originalMatched $primaryMatched $compositeAliases [llength $names] [expr {[llength $names]-$present}]]
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
        dict set importMetrics srl_violations $violations
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
            dict set importMetrics cascade_violations $violations
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
    set f [open [file join $out import_acceptance.tsv] w]
    dict for {key value} $importMetrics {puts $f "$key\t$value"}
    puts $f "policy\t$importPolicy";close $f
    if {$importPolicy eq "strict"} {require_legal_amf_import $importMetrics}
    if {$backendMode eq "import-only"} {
        puts "AMF_IMPORT_ONLY_FINISHED";close $timeline;exit 0
    }
    set clockRelease [file join $placement clock_buffer_release.tsv]
    if {[file exists $clockRelease]} {
        timed clock_buffer_release {
            set f [open $clockRelease r];gets $f header
            while {[gets $f line] >= 0} {
                lassign [split $line "\t"] name site
                set cell [get_cells -quiet $name]
                if {[llength $cell] != 1 || [get_property REF_NAME $cell] ne "BUFGCE" || [get_property LOC $cell] ne $site} {
                    error "Clock buffer release does not match the audited input: $name"
                }
                set releasedClockBuffers($name) $site
                set_property IS_LOC_FIXED false $cell
                set_property IS_BEL_FIXED false $cell
                reset_property LOC $cell
                reset_property BEL $cell
                unplace_cell $cell
            }
            close $f
            puts "AMF_CLOCK_BUFFERS_RELEASED [array size releasedClockBuffers]"
        }
    }
    timed place {place_design}
    timed place_audit {audit placed}
    timed placed_checkpoint {write_checkpoint [file join $out getrf_placed.dcp]}
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
    if {$physicalAudit} {
        source [file join [file dirname [info script]] export_boundary_timing_samples.tcl]
        timed boundary_samples {export_boundary_timing_samples [file join $out physical]}
    }
    puts "AMF_FULL_FLOW_FINISHED"
} failure options]} {
    puts stderr "AMF_FULL_FLOW_FAILED: $failure"
    puts stderr [dict get $options -errorinfo]
    close $timeline
    exit 2
}
close $timeline
exit 0
