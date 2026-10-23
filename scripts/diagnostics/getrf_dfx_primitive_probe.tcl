open_checkpoint [lindex $argv 0]
set dsps [get_cells -hierarchical -filter {REF_NAME == DSP48E2}]
set d [lindex $dsps 0]
puts "DSP $d"
report_property $d
set sub [lindex [get_cells -hierarchical -filter {REF_NAME == DSP_OUTPUT}] 0]
puts "INTERNAL $sub";report_property $sub
foreach c [list $d $sub] {
 foreach p [lrange [get_pins -of_objects $c -filter {DIRECTION == OUT}] 0 2] {
  set n [get_nets -quiet -top_net_of_hierarchical_group -of_objects $p]
  puts "PIN $p NET $n"
  if {[llength $n]} {puts "DRIVER [get_pins -quiet -leaf -of_objects $n -filter {DIRECTION == OUT}]"}
 }
}
set all [get_cells -hierarchical -filter {IS_PRIMITIVE}]
set counts [dict create]
foreach c $all {dict incr counts [list [get_property REF_NAME $c] [get_property PRIMITIVE_LEVEL $c]]}
dict for {key value} $counts {puts "TYPE $key COUNT $value"}
exit 0

