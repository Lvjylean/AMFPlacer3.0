# Read-only extraction. No placement, routing, optimization or checkpoint writes.
if {$argc != 4} {error "Expected: input.dcp output.tsv dcp_sha256 netlist_sha256"}
lassign $argv checkpoint output dcp_hash netlist_hash
foreach digest [list $dcp_hash $netlist_hash] {
    if {![regexp {^[0-9a-f]{64}$} $digest]} {error "Invalid SHA-256"}
}
if {[file exists $output] || [file exists "$output.tmp"]} {error "Refusing to overwrite DSP metadata"}
set_param general.maxThreads 2
open_checkpoint $checkpoint
set f [open "$output.tmp" w]
puts $f "AMF_DSP_TIMING 1"
puts $f "source_dcp_sha256 $dcp_hash"
puts $f "netlist_sha256 $netlist_hash"
puts $f "cell\tPREG\tAREG\tACASCREG\tBREG\tBCASCREG\tMREG\tCREG\tDREG\tADREG"
set cells [lsort [get_cells -hierarchical -filter {REF_NAME == DSP48E2}]]
foreach cell $cells {
    set row [list $cell]
    foreach property {PREG AREG ACASCREG BREG BCASCREG MREG CREG DREG ADREG} {
        set value [get_property $property [get_cells $cell]]
        if {![string is integer -strict $value]} {error "Missing $property for $cell"}
        lappend row $value
    }
    puts $f [join $row "\t"]
}
close $f
file rename "$output.tmp" $output
puts "AMF_DSP_TIMING_EXPORTED [llength $cells]"
close_design
exit
