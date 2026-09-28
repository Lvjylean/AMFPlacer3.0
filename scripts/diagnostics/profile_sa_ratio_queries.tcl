# Small query profiler; never modifies the opened design.
lassign $argv input requests out
set_param general.maxThreads 4
open_checkpoint $input
set f [open $requests r];gets $f header
set g [open $out w]
puts $g "sample\tstep\tmicroseconds"
proc step {name body} {
    global g i
    set t [clock microseconds]
    uplevel 1 $body
    puts $g "$i\t$name\t[expr {[clock microseconds]-$t}]";flush $g
}
for {set i 0} {$i<12 && [gets $f line]>=0} {incr i} {
    lassign [split $line "\t"] source sink
    step pins {set sp [get_pins -quiet $source];set tp [get_pins -quiet $sink]}
    step net {set net [get_nets -quiet -of_objects $tp]}
    step driver {set driver [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == OUT}]}
    step clocks {set c [get_clocks -quiet -of_objects $net]}
    step fanout {set fo [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]}
    step cells {set sc [get_cells -quiet -of_objects $sp];set tc [get_cells -quiet -of_objects $tp]}
    step props {set a [get_property LOC $sc];set b [get_property LOC $tc]}
    step delay {set delay [get_net_delays -quiet -of_objects $net -to $tp]}
    step value {set ps [get_property SLOW_MAX $delay]}
    step whole_net_delay {set allDelays [get_net_delays -of_objects $net]}
}
close $f;close $g
report_property $delay
exit
