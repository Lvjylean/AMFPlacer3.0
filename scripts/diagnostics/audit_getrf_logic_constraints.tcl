# Read-only DCP audit; import replay writes only into the independent audit directory.
set_param general.maxThreads 2
lassign $argv root run output mode
file mkdir $output
set events [open [file join $output audit-events.tsv] w]
puts $events "stage\tcheck\tstatus\telapsed_seconds\tdetail"
proc checked {stage name body} {
    global events
    set begin [clock milliseconds]
    puts "AUDIT_START $stage $name"; flush stdout
    set code [catch {uplevel 1 $body} result options]
    set detail [string map [list "\n" " | " "\r" " " "\t" " "] $result]
    if {[string length $detail] > 1500} {set detail [string range $detail 0 1499]}
    puts $events "$stage\t$name\t$code\t[expr {([clock milliseconds]-$begin)/1000.0}]\t$detail"
    flush $events
    puts "AUDIT_DONE $stage $name code=$code"; flush stdout
    return $code
}
proc inventory {directory} {
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    set names [get_property NAME $cells]
    set types [get_property REF_NAME $cells]
    set locs [get_property LOC $cells]
    set bels [get_property BEL $cells]
    set f [open [file join $directory cells.tsv] w]
    puts $f "cell\ttype\tloc\tbel"
    foreach name $names type $types loc $locs bel $bels {puts $f "$name\t$type\t$loc\t$bel"}
    close $f
    set f [open [file join $directory ports.tsv] w]
    puts $f "port\tdirection"
    foreach port [get_ports] {puts $f "[get_property NAME $port]\t[get_property DIRECTION $port]"}
    close $f
    report_property -all [current_design] -file [file join $directory design-properties.rpt]
}
proc export_stage {stage fullChecks} {
    global output
    set directory [file join $output $stage]
    file mkdir $directory
    if {[checked $stage inventory {inventory $directory}]} {error "Inventory export failed"}
    if {[checked $stage edif {write_edif [file join $directory logical.edf]}]} {error "Functional EDIF export failed"}
    checked $stage timing_xdc {write_xdc -type timing [file join $directory timing.xdc]}
    checked $stage clocks {report_clocks -file [file join $directory clocks.rpt]}
    if {$stage ne "imported"} {
        checked $stage check_timing {check_timing -verbose -file [file join $directory check-timing.rpt]}
        checked $stage exceptions_summary {report_exceptions -summary -file [file join $directory exceptions-summary.rpt]}
        checked $stage exceptions_coverage {report_exceptions -coverage -file [file join $directory exceptions-coverage.rpt]}
        checked $stage exceptions_ignored {report_exceptions -ignored -file [file join $directory exceptions-ignored.rpt]}
        checked $stage disabled_timing {report_disable_timing -file [file join $directory disabled-timing.rpt]}
    }
    if {$fullChecks} {
        checked $stage place_status {report_place_status -file [file join $directory place-status.rpt]}
        checked $stage route_status {report_route_status -ignore_cache -file [file join $directory route-status.rpt]}
        checked $stage drc {report_drc -no_waivers -file [file join $directory drc-no-waivers.rpt]}
        checked $stage methodology {report_methodology -no_waivers -file [file join $directory methodology.rpt]}
        checked $stage timing_summary {report_timing_summary -delay_type min_max -report_unconstrained -max_paths 20 -file [file join $directory timing-summary.rpt]}
        checked $stage clock_interaction {report_clock_interaction -file [file join $directory clock-interaction.rpt]}
        checked $stage cdc {report_cdc -details -file [file join $directory cdc.rpt]}
    }
}
if {[catch {
    if {$mode eq "input_import"} {
        open_checkpoint [file join $root data reference getrf-u250 post_opt.dcp]
        export_stage input 0
        set amf3_import_error_events 0
        set replay [file join $output import-replay import_placement.tcl]
        if {[checked imported placement_replay {source $replay}]} {error "Import replay failed"}
        set f [open [file join $output import-replay rejection-count.txt] w]
        puts $f $amf3_import_error_events; close $f
        export_stage imported 0
        close_design
    } elseif {$mode eq "placed_routed"} {
        open_checkpoint [file join $run reports getrf_placed.dcp]
        export_stage placed 0
        close_design
        open_checkpoint [file join $run reports getrf_routed.dcp]
        export_stage routed 1
        close_design
    } else {error "Unknown audit mode $mode"}
} message options]} {
    puts stderr [dict get $options -errorinfo]
    close $events
    exit 2
}
close $events
puts "AUDIT_FINISHED $mode"
exit 0
