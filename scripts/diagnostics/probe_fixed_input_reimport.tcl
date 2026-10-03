# Verify the legacy exporter's unplace step plus declared fixed-site restoration.
if {$argc != 3} {error "Expected input.dcp fixed_units output.json"}
lassign $argv input fixed output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    place_design -unplace
    set f [open $fixed r];gets $f header
    set checked 0
    while {[gets $f line] >= 0} {
        lassign $line key name key site key bel
        place_cell [list $name $site]
        set cell [get_cells $name]
        set actual_site [get_property LOC $cell]
        set actual_bel [get_property BEL $cell]
        if {$actual_site ne $site || $actual_bel ne $bel} {
            error "Fixed input mismatch: $name expected=$site/$bel actual=$actual_site/$actual_bel"
        }
        incr checked
    }
    close $f
    set f [open $output w]
    puts $f [format {{"checked":%d,"loc_bel_mismatches":0,"placement_scope":"declared fixed inputs only"}} $checked]
    close $f
    puts "ISPD_FIXED_INPUT_REIMPORT_VERIFIED $checked"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
