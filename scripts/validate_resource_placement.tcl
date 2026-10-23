# Vivado audit of partial AMF hard-resource placement. Arguments: input DCP, AMF run, output directory.
if {$argc != 3} { error "Expected: input.dcp resource-directory output-directory" }
lassign $argv input resource_dir out
file mkdir $out
set_param general.maxThreads 4
set start [clock milliseconds]
if {[catch {
    open_checkpoint $input
    set opened [clock milliseconds]
    source [file join $resource_dir place_resources.tcl]
    set placed [clock milliseconds]
    set requested [open [file join $resource_dir resources.tsv] r]
    gets $requested header
    set audit [open [file join $out placement_audit.tsv] w]
    puts $audit "cell\ttype\trequested_site\tactual_site\tactual_bel\tactual_slr"
    set count 0
    while {[gets $requested line] >= 0} {
        if {$line eq ""} { continue }
        lassign [split $line "\t"] name type site bel slr
        set cell [get_cells -quiet $name]
        if {[llength $cell] != 1} { error "Expected one cell: $name" }
        set actual [get_property LOC $cell]
        set actual_bel [get_property BEL $cell]
        if {$actual ne $site} { error "LOC mismatch for $name: requested $site, actual $actual" }
        if {$type eq "URAM288" || $type eq "URAM288_BASE"} {
            if {[lindex [split [file tail $actual_bel] .] end] ne "URAM_288K_INST"} { error "URAM BEL mismatch: $name $actual_bel" }
        }
        set actual_slr [get_slrs -quiet -of_objects [get_sites $actual]]
        if {$actual_slr ne "SLR$slr"} { error "SLR mismatch for $name: expected SLR$slr actual $actual_slr" }
        puts $audit "$name\t$type\t$site\t$actual\t$actual_bel\t$actual_slr"
        incr count
    }
    close $requested
    close $audit
    report_clocks -file [file join $out clocks.rpt]
    write_checkpoint [file join $out getrf_hard_resources_partial.dcp]
    set finished [clock milliseconds]
    set report [open [file join $out validation.json] w]
    puts $report [format {{"schema":"amf-vivado-resource-audit-v1","matched_cells":%d,"open_seconds":%.3f,"place_seconds":%.3f,"audit_and_checkpoint_seconds":%.3f,"vivado_version":"%s","full_placement_executed":false,"routing_executed":false}} $count [expr {($opened-$start)/1000.0}] [expr {($placed-$opened)/1000.0}] [expr {($finished-$placed)/1000.0}] [version -short]]
    close $report
    puts "AMF_VIVADO_RESOURCE_AUDIT_OK $count"
} failure options]} {
    puts stderr "AMF_VIVADO_RESOURCE_AUDIT_FAILED: $failure"
    puts stderr [dict get $options -errorinfo]
    exit 2
}
exit 0
