lassign $argv input out
set_param general.maxThreads 4
set f [open [file join $out tools.txt] w]; puts $f [version]; close $f
puts "FANOUT_OPEN_START"; flush stdout
open_checkpoint $input
puts "FANOUT_REPORT_START"; flush stdout
report_high_fanout_nets -max_nets 20 -load_types -file [file join $out nonclock_fanout.rpt]
report_high_fanout_nets -max_nets 20 -clocks [get_clocks] -load_types -file [file join $out including_clocks_fanout.rpt]
report_high_fanout_nets -max_nets 5 -slr -file [file join $out nonclock_slr_fanout.rpt]
puts "FANOUT_DIAGNOSTIC_DONE"; flush stdout
close_design
exit
