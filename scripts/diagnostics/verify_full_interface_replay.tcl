# Exercise the backend's physical replay without running placement or routing.
if {$argc != 3} {error "Expected prepared.dcp fixed_cells.tsv new-output-directory"}
lassign $argv input fixed output
if {[file exists $output]} {error "Refusing to overwrite replay evidence"}
file mkdir $output
set scriptDir [file dirname [file dirname [info script]]]
source [file join $scriptDir full_design_interfaces.tcl]
source [file join $scriptDir full_interface_contract.tcl]
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    amf_contract::capture
    set f [open $fixed r];gets $f header
    if {$header ne "cell\tprimitive\tsite\tbel\tfixed\ttarget_mode"} {error "Invalid fixed interface schema"}
    set rows {};set names {}
    while {[gets $f line] >= 0} {
        lassign [split $line "\t"] name kind site bel isfixed mode
        if {[get_property REF_NAME [get_cells $name]] ne $kind || [amf_full::target_mode $kind] ne $mode} {error "Interface type mismatch: $name"}
        lappend rows [list $name $site $bel $mode];lappend names $name
    }
    close $f
    set_property IS_LOC_FIXED false [get_cells $names]
    set_property IS_BEL_FIXED false [get_cells $names]
    unplace_cell [get_cells $names]
    set f [open [file join $output fixed_contract.tsv] w]
    puts $f "cell\tsite\tbel\ttarget_mode"
    foreach row $rows {
        lassign $row name site bel mode
        set target $site
        if {$mode eq "bel"} {set target "$site/$bel"}
        place_cell [list $name $target]
        set_property IS_LOC_FIXED true [get_cells $name]
        if {$mode eq "bel"} {set_property IS_BEL_FIXED true [get_cells $name]}
        puts $f [join $row "\t"]
    }
    close $f
    # Placing related I/O primitives can clear earlier PHY BEL-lock flags.
    # Apply the final locks only after every physical assignment is restored.
    foreach row $rows {
        lassign $row name site bel mode
        set c [get_cells $name]
        set_property IS_LOC_FIXED true $c
        if {$mode eq "bel"} {set_property IS_BEL_FIXED true $c}
    }
    set f [open [file join $output replay_observed.tsv] w]
    puts $f "cell\tprimitive\texpected_site\texpected_bel\tmode\tactual_site\tactual_bel\tloc_fixed\tbel_fixed"
    foreach row $rows {
        lassign $row name site bel mode
        set c [get_cells $name]
        puts $f [join [list $name [get_property REF_NAME $c] $site $bel $mode [get_property LOC $c] [get_property BEL $c] [get_property IS_LOC_FIXED $c] [get_property IS_BEL_FIXED $c]] "\t"]
    }
    close $f
    amf_contract::check $output $output replayed
    puts "FULL_INTERFACE_REPLAY_PASS [llength $rows]"
} message options]} {
    puts stderr $message;puts stderr [dict get $options -errorinfo];exit 1
}
exit 0
