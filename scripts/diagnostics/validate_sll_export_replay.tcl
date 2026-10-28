# Replay the actual exported file on the frozen pre-SLL checkpoint.
set out [file normalize [lindex $argv 0]];file mkdir $out
lassign [lrange $argv 1 end] checkpoint xdc selections
set_param general.maxThreads 4
proc route_design {args} {error "Routing is forbidden in the export replay validation"}
open_checkpoint $checkpoint
set clocks [get_clocks]
if {[llength $clocks]!=1 || abs([get_property PERIOD $clocks]-10.0)>1e-6} {error "Clock changed"}
set f [open $selections r];gets $f header
set names {}
while {[gets $f line]>=0} {lappend names [lindex [split $line "\t"] 0]}
close $f
set objects [get_nets $names]
if {[llength $objects]!=8442} {error "Net count mismatch"}
set inherited 0
set f [open $out/inherited-fixed-routes.tsv w];puts $f "net\tfixed_route"
foreach net $objects {
 set fixed [get_property FIXED_ROUTE $net]
 if {[string trim $fixed "{} \t\r\n"] ne ""} {incr inherited;puts $f "$net\t$fixed"}
}
close $f
# Clear only the target nets in this disposable in-memory replay. The input
# DCP is never overwritten. Native placement may already carry fixed routes.
reset_property FIXED_ROUTE $objects
foreach net $objects {if {[string trim [get_property FIXED_ROUTE $net] "{} \t\r\n"] ne ""} {error "Failed to clear replay target $net"}}
set started [clock milliseconds]
source $xdc
set seconds [expr {([clock milliseconds]-$started)/1000.0}]
set count 0
foreach net $objects {if {[string trim [get_property FIXED_ROUTE $net] "{} \t\r\n"] eq ""} {error "Export replay missed $net"};incr count}
set f [open $out/replay-summary.tsv w]
puts $f "inherited_fixed_routes\t$inherited"
puts $f "fixed_routes_before\t0"
puts $f "fixed_routes_after\t$count"
puts $f "source_xdc_seconds\t$seconds"
puts $f "routing_executed\t0"
puts $f "clock_period_ns\t[get_property PERIOD $clocks]"
close $f
puts "E2_EXPORTED_XDC_REPLAY_VERIFIED $count"
exit 0
