# Export the actual fabric sites of an open checkpoint, without running placement.
# Usage: ... -tclargs input.dcp out_dir
#     or ... -tclargs --part xcu250-figd2104-2L-e out_dir
set_param general.maxThreads 4
set partMode [expr {[llength $argv] == 3 && [lindex $argv 0] eq "--part"}]
if {!$partMode && [llength $argv] != 2} {error "Expected input.dcp out_dir, or --part part out_dir"}
set out [file normalize [lindex $argv end]]
if {[file exists $out]} {error "Refusing to overwrite $out"}
file mkdir $out
if {[catch {
    if {$partMode} {
        create_project -in_memory -part [lindex $argv 1]
        set rtl [open $out/device_probe.v w]
        puts $rtl {module amf_device_probe(input wire a, output wire y); assign y = a; endmodule}
        close $rtl
        read_verilog $out/device_probe.v
        synth_design -top amf_device_probe -mode out_of_context
    } else {
        open_checkpoint [file normalize [lindex $argv 0]]
    }
    set meta [open $out/metadata.tsv w]
    puts $meta "part\t[get_property PART [current_design]]"
    puts $meta "vivado\t[version -short]"
    puts $meta "scope\tfabric-sites"
    puts $meta "availability_source\t[expr {$partMode ? {empty-device-design} : {input-checkpoint}}]"
    puts $meta "slrs\t[join [get_slrs] ,]"
    close $meta
    set crSlr [dict create]
    foreach slr [get_slrs] {
        if {![regexp {^SLR([0-9]+)$} $slr -> id]} {error "Unexpected SLR name $slr"}
        foreach cr [get_clock_regions -of_objects $slr] {dict set crSlr $cr $id}
    }
    set output [open $out/sites.tsv w]
    puts $output "site\ttile\tclock_region\tsite_type\ttile_type\trpm_x\trpm_y\tslr\tprohibited\tbels"
    set n 0
    # Avoid repeatedly copying a growing Tcl dictionary in Vivado's interpreter.
    array set tileTypes {}
    foreach site [get_sites -quiet {SLICE_* DSP48E2_* RAMB18_* RAMB36_* URAM288_*}] {
        set cr [get_property CLOCK_REGION $site]
        if {![dict exists $crSlr $cr]} {error "Missing SLR for $site / $cr"}
        set tile [get_tiles -of_objects $site]
        if {[llength $tile] != 1} {error "Ambiguous tile for $site"}
        if {![info exists tileTypes($tile)]} {set tileTypes($tile) [get_property TYPE $tile]}
        set prohibited [expr {[get_property PROHIBIT $site] ? 1 : 0}]
        puts $output [join [list $site $tile $cr [get_property SITE_TYPE $site] \
            $tileTypes($tile) [get_property RPM_X $site] [get_property RPM_Y $site] \
            [dict get $crSlr $cr] $prohibited [join [get_bels -of_objects $site] ,]] "\t"]
        incr n
        if {$n % 20000 == 0} {puts "AMF_DEVICE_EXPORT_PROGRESS=$n"}
    }
    close $output
    close_design
    puts "AMF_DEVICE_EXPORT_OK=$n"
} msg opts]} {
    puts stderr $msg
    puts stderr [dict get $opts -errorinfo]
    exit 1
}
exit 0
