lassign $argv helper input output
source $helper
set f [open $input r];set out [open $output w];set count 0
while {[gets $f line]>=0} {
 if {[llength $line]!=4} {error "Unexpected old export command"}
 lassign $line command property route net
 if {$command ne "set_property" || $property ne "FIXED_ROUTE"} {error "Unexpected old export operation"}
 puts $out [e2_sll::constraint_command $route $net]
 incr count
}
close $f;close $out
if {$count!=8442} {error "Unexpected export count: $count"}
puts "E2_EXPORT_COMMANDS_REGENERATED $count"
