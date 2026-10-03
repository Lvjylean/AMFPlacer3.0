# Retarget a cache-compatible ISPD checkpoint through Vivado's EDIF flow.
# This is a part-level input preflight, not placement, routing or board signoff.
if {$argc != 3} {error "Expected compatible input.dcp, new output directory, original top module"}
lassign $argv input output top
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
proc leaf_identity {path} {
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    set names [get_property NAME $cells]
    set types [get_property REF_NAME $cells]
    array set primitive_names {}
    foreach name $names {set primitive_names($name) 1}
    set rows {}
    foreach name $names kind $types {
        set parent $name
        set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            if {[info exists primitive_names($parent)]} {set internal 1;break}
        }
        if {!$internal} {lappend rows "$name\t$kind"}
    }
    set rows [lsort $rows]
    set f [open $path w]
    puts $f "cell\tprimitive"
    foreach row $rows {puts $f $row}
    close $f
    return $rows
}
if {[catch {
    open_checkpoint $input
    set original [leaf_identity [file join $output original_leaves.tsv]]
    set original_ports [lsort [get_property NAME [get_ports]]]
    set original_clocks [llength [get_clocks -quiet *]]
    write_xdc -type timing [file join $output original_timing.xdc]
    # VU095 pin/site names do not define legal U250 pin/site assignments.
    # Preserve an explicit record before clearing only physical assignments.
    write_xdc -type physical [file join $output source_physical.xdc]
    set ports [get_ports -quiet -filter {PACKAGE_PIN != ""}]
    if {[llength $ports]} {reset_property PACKAGE_PIN $ports}
    set cells [get_cells -hier -quiet -filter {LOC != ""}]
    if {[llength $cells]} {reset_property LOC $cells}
    set cells [get_cells -hier -quiet -filter {BEL != ""}]
    if {[llength $cells]} {reset_property BEL $cells}
    puts "ISPD_RETARGET_STAGE export_portable_edif"
    write_edif [file join $output ${top}.edf]
    close_project
    create_project -in_memory -part xcu250-figd2104-2L-e
    read_edif [file join $output ${top}.edf]
    link_design -top $top -part xcu250-figd2104-2L-e
    read_xdc [file join $output original_timing.xdc]
    set migrated [leaf_identity [file join $output u250_leaves.tsv]]
    if {$migrated ne $original} {error "Logical leaf name/type identities changed during retargeting"}
    if {[lsort [get_property NAME [get_ports]]] ne $original_ports} {error "Top-level port identities changed"}
    if {[llength [get_clocks -quiet *]] != $original_clocks} {error "Clock count changed"}
    set blackboxes [get_cells -hier -quiet -filter {IS_BLACKBOX}]
    if {[llength $blackboxes]} {error "Retargeted design contains black boxes: $blackboxes"}
    set ports [get_ports]
    set f [open [file join $output ports.tsv] w]
    puts $f "port\tdirection\tpackage_pin\tiostandard"
    foreach name [get_property NAME $ports] direction [get_property DIRECTION $ports] pin [get_property PACKAGE_PIN $ports] standard [get_property IOSTANDARD $ports] {
        puts $f "$name\t$direction\t$pin\t$standard"
    }
    close $f
    report_clocks -file [file join $output clocks.rpt]
    report_utilization -file [file join $output utilization.rpt]
    write_checkpoint [file join $output u250_unplaced.dcp]
    set f [open [file join $output summary.tsv] w]
    puts $f "part\t[get_property PART [current_design]]"
    puts $f "logical_leaves\t[llength $migrated]"
    puts $f "ports\t[llength $original_ports]"
    puts $f "clock_count\t$original_clocks"
    puts $f "leaf_identity_equal\ttrue"
    puts $f "blackboxes\t0"
    puts $f "placed\tfalse"
    puts $f "routed\tfalse"
    puts $f "board_pinout_applied\tfalse"
    close $f
    puts "ISPD_U250_RETARGET_OK"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
