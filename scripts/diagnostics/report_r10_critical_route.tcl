# Read-only follow-up for report options supported by Vivado 2024.2.
set_param general.maxThreads 4
lassign $argv input out
open_checkpoint $input
set paths [get_timing_paths -setup -max_paths 1 -nworst 1]
report_design_analysis -timing -of_timing_paths $paths -show_all -routed_vs_estimated -file [file join $out path_analysis.rpt]
report_property -all $paths -file [file join $out timing_path_properties.rpt]
set dst [get_pins {grp_getrf_core_double_2_512_256_s_fu_1395/grp_subUpdate_double_2_512_s_fu_6074/mul_9ns_2ns_11_1_1_U1576/dout[0]_INST_0_i_14/I3}]
report_property -all $dst -file [file join $out sink_properties.rpt]
report_timing -of_objects $paths -input_pins -file [file join $out net_timing.rpt]
set f [open [file join $out supplemental_done.txt] w];puts $f [clock format [clock seconds]];close $f
exit 0
