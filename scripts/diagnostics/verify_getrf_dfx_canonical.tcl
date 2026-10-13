set dcp [lindex $argv 0]
set common [lindex $argv 1]
set output [lindex $argv 2]
source $common
open_checkpoint $dcp
set canonical [dict create]; set mapping [dict create]; set refs [dict create]
foreach c [amf_dfx::primitive_cells] {
    set kind [get_property REF_NAME $c];dict set canonical $c $kind;dict incr refs $kind
}
foreach c [dict keys $canonical] {
    foreach pin [get_pins -of_objects $c -filter {DIRECTION == OUT}] {
        set net [get_nets -quiet -top_net_of_hierarchical_group -of_objects $pin]
        if {[llength $net]} {dict lappend mapping $net $pin}
    }
}
set checked 0;set dspDrivers 0
dict for {net pins} $mapping {
    set got [amf_dfx::canonical_net_driver $net $canonical]
    if {[lsearch -exact $pins [lindex $got 0]]<0} {error "Canonical driver lost: $net : $got vs $pins"}
    if {[lindex $got 2] eq "DSP48E2"} {incr dspDrivers}
    incr checked
}
if {$dspDrivers<10} {error "DSP connections not exercised"}
set f [open $output w]
puts $f "canonical_primitives\t[dict size $canonical]"
puts $f "canonical_nets_checked\t$checked"
puts $f "DSP_driver_nets\t$dspDrivers"
dict for {type count} $refs {puts $f "$type\t$count"}
close $f
puts "PASS canonical GETRF module: primitives=[dict size $canonical] nets=$checked DSP_nets=$dspDrivers"
exit 0

