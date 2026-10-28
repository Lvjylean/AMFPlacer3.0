# Read-only diagnosis of one receiver rejected by the post-route graph audit.
lassign $argv checkpoint helper out
file mkdir $out
set_param general.maxThreads 2
open_checkpoint $checkpoint
source $helper
e2_sll::refresh_access_index
set receiver {grp_getrf_core_double_2_512_256_s_fu_1395/grp_getrf_core_double_2_512_256_Pipeline_LoopRows_fu_3713/sparsemux_513_8_64_1_1_U525/dout[43]_INST_0_i_93/I2}
set pin [e2_sll::pin_object $receiver]
set net [e2_sll::driver_net $receiver]
set sources [get_pins -leaf -of_objects $net -filter {DIRECTION == OUT}]
set f [open $out/objects.txt w]
puts $f "receiver $receiver\nnet $net\nroute_status [get_property ROUTE_STATUS $net]\nsources $sources"
foreach item [concat [list $pin] $sources] {
 puts $f "PIN $item";puts $f [report_property -all -return_string $item]
 set cell [get_cells -of_objects $item]
 puts $f "CELL $cell LOC [get_property LOC $cell] BEL [get_property BEL $cell]"
 foreach b [get_bel_pins -of_objects $item] {puts $f "BEL_PIN $b";puts $f [report_property -all -return_string $b]}
 foreach s [get_site_pins -of_objects $item] {puts $f "SITE_PIN $s";puts $f [report_property -all -return_string $s]}
 puts $f "NODES [get_nodes -of_objects [get_site_pins -of_objects $item]]"
}
puts $f "SLL [get_nodes -of_objects $net -filter {IS_SLL}]"
close $f
set f [open $out/route.txt w];puts $f [get_property ROUTE $net];close $f
set f [open $out/nodes.txt w];foreach n [get_nodes -of_objects $net] {puts $f $n};close $f
set f [open $out/edges.tsv w];puts $f "pip\tuphill\tdownhill\tdirectional"
foreach p [get_pips -of_objects $net] {puts $f [join [list $p [get_nodes -uphill -of_objects $p] [get_nodes -downhill -of_objects $p] [get_property IS_DIRECTIONAL $p]] "\t"]};close $f
set f [open $out/terminals.tsv w];puts $f "pin\tdirection\tnode"
foreach p [concat [list $pin] $sources] {foreach n [get_nodes -of_objects [get_site_pins -of_objects $p]] {puts $f [join [list $p [get_property DIRECTION $p] $n] "\t"]}};close $f
puts "READ_ONLY_RECEIVER_INSPECTION_COMPLETE"
exit
