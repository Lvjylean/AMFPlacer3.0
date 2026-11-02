if {$argc != 2} {error "Expected checkpoint new-output-directory"}
lassign $argv checkpoint output
if {[file exists $output]} {error "Refusing to overwrite FF metadata"}
file mkdir $output
set scripts [file dirname [file dirname [info script]]]
source [file join $scripts full_design_interfaces.tcl]
source [file join $scripts full_ff_controls.tcl]
set_param general.maxThreads 4
if {[catch {
    set before [lindex [exec sha256sum $checkpoint] 0]
    open_checkpoint $checkpoint
    amf_full::export_ff_controls $output
    if {$before ne [lindex [exec sha256sum $checkpoint] 0]} {error "Checkpoint changed during export"}
    set f [open [file join $output binding.tsv] w]
    puts $f "input_dcp_sha256\t$before"
    foreach name {ff_control_counts.tsv ff_control_inversions.tsv} {
        puts $f "$name\t[lindex [exec sha256sum [file join $output $name]] 0]"
    }
    close $f
} message options]} {
    puts stderr $message; puts stderr [dict get $options -errorinfo]; exit 1
}
exit 0
