if {$argc != 2} {error "Expected input.dcp output-directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open [file join $output dsp_registers.tsv] w]
    puts $f "cell\tAREG\tACASCREG\tBREG\tBCASCREG\tCREG\tDREG\tADREG\tMREG\tPREG\tINMODEREG\tOPMODEREG\tALUMODEREG\tCARRYINREG\tCARRYINSELREG"
    foreach cell [get_cells -hier -filter {REF_NAME == DSP48E2}] {
        set row [list $cell]
        foreach prop {AREG ACASCREG BREG BCASCREG CREG DREG ADREG MREG PREG INMODEREG OPMODEREG ALUMODEREG CARRYINREG CARRYINSELREG} {
            lappend row [get_property $prop $cell]
        }
        puts $f [join $row "\t"]
    }
    close $f
    report_drc -checks [get_drc_checks LUTLP-1] -file [file join $output combinational_loops.rpt]
    check_timing -verbose -file [file join $output check_timing.rpt]
    report_timing_summary -delay_type max -max_paths 3 -report_unconstrained -file [file join $output timing_summary.rpt]
    puts "ISPD_DSP_TIMING_PROBE_COMPLETED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
