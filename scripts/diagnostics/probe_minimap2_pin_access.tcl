set_param general.maxThreads 4
open_checkpoint [lindex $argv 0]
set c [lindex [get_cells -hier -filter {IS_PRIMITIVE && REF_NAME == FDRE && NAME =~ *udma_wrapper/*}] 0]
set pins [get_pins -of_objects $c]
set p [lindex $pins 0]
set n [get_nets -of_objects $p]
puts "PIN_PROPERTIES [list_property $p]"
puts "NET_PROPERTIES [list_property $n]"
foreach prop [list_property $p] {puts "PIN_PROPERTY $prop [get_property $prop $p]"}
foreach prop [list_property $n] {puts "NET_PROPERTY $prop [get_property $prop $n]"}
puts "TIME_NORMAL [time {foreach p $pins {get_nets -quiet -of_objects $p}} 100]"
puts "TIME_TOP [time {foreach p $pins {get_nets -quiet -top_net_of_hierarchical_group -of_objects $p}} 10]"
puts "TIME_PROPS [time {foreach p $pins {get_property REF_PIN_NAME $p;get_property DIRECTION $p}} 100]"
puts "PIN_ACCESS_PROBE_DONE"
exit
