# Compare the logical leaf identities after reopening a DCP in another Vivado version.
if {[llength $argv] != 2} {error "Expected input DCP and new output TSV"}
set output [lindex $argv 1]
if {[file exists $output]} {error "Refusing to overwrite $output"}
set_param general.maxThreads 4
if {[catch {
    open_checkpoint [lindex $argv 0]
    # Batched equivalent of the legacy recursive leaf traversal: preserve a
    # primitive macro as one cell and omit its internal unisim descendants.
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE && REF_NAME != VCC && REF_NAME != GND}]
    set names [get_property NAME $cells]
    set types [get_property REF_NAME $cells]
    array set primitiveNames {}
    foreach name $names {set primitiveNames($name) 1}
    set f [open $output w]
    puts $f "cell_name\tprimitive"
    set count 0
    foreach name $names type $types {
        set parent $name
        set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash - 1}]]
            if {[info exists primitiveNames($parent)]} {set internal 1; break}
        }
        if {!$internal} {puts $f "$name\t$type"; incr count}
    }
    close $f
    puts "AMF_LEAF_IDENTITY_OK=$count"
    close_design
} msg opts]} {
    puts stderr $msg
    puts stderr [dict get $opts -errorinfo]
    exit 1
}
exit 0
