set out [file normalize [lindex $argv 0]];file mkdir $out
set_param general.maxThreads 2
open_checkpoint [lindex $argv 1]
set f [open [lindex $argv 2] r];set wanted [string trim [read $f]];close $f
set cell [get_cells [file dirname $wanted]]
set pin [get_pins -of_objects $cell -filter "NAME == $wanted"]
report_property -all -file $out/cell-properties.txt $cell
report_property -all -file $out/pin-properties.txt $pin
set f [open $out/mapping.txt w]
puts $f [list cell $cell bels [get_bels -of_objects $cell]]
set net [get_nets -parent_net -segments -of_objects $pin]
puts $f [list net $net driver [get_pins -leaf -of_objects $net -filter {DIRECTION == OUT}] receiver_count [llength [get_pins -leaf -of_objects $net -filter {DIRECTION == IN}]]]
puts $f [list net_leaf_inputs [get_pins -leaf -of_objects $net -filter {DIRECTION == IN}]]
foreach method {cell_filter full_name hierarchical_filter} {
 if {$method eq "cell_filter"} {set q $pin} elseif {$method eq "full_name"} {set q [get_pins -quiet $wanted]} else {set q [get_pins -hierarchical -filter "NAME == $wanted"]}
 puts $f [list method $method objects $q]
 if {[llength $q]} {puts $f [list method $method is_leaf [get_property IS_LEAF $q] is_orig [get_property IS_ORIG_PIN $q] bel_pins [get_bel_pins -quiet -of_objects $q] site_pins [get_site_pins -quiet -of_objects $q]]}
}
foreach p [get_pins -of_objects $cell -filter {REF_PIN_NAME =~ A*}] {
 set bp [get_bel_pins -quiet -of_objects $p]
 set sp [get_site_pins -quiet -of_objects $p]
 set bn {};if {[llength $bp]} {set bn [get_nodes -quiet -of_objects $bp]}
 puts $f [list pin $p bel_pins $bp site_pins $sp bel_nodes $bn]
 foreach b $bp {puts $f [list bel_pin $b is_used [get_property IS_USED $b]]}
}
set site [get_sites [get_property LOC $cell]]
puts $f [list site $site pins [get_site_pins -of_objects $site]]
set bp [get_bel_pins -of_objects [get_bels $site/DSP_A_B_DATA]]
puts $f [list physical_bel_pins $bp]
puts $f [list physical_logical_pins [get_pins -quiet -of_objects $bp]]
close $f
source [lindex $argv 3]
proc search {items prefix} {
 global wanted record out index
 foreach c $items {
  if {[dict exists $c pin]} {
   if {[dict get $c pin] eq $wanted} {
    set f [open $out/plan-context.txt w]
    puts $f [list index $index driver [dict get $record driver] source_slr [dict get $record source_slr] receiver_slr [dict get $c slr] sll_prefix $prefix]
    close $f
   }
  } else {search [dict get $c children] [concat $prefix [list [dict get $c resource]]]}
 }
}
set index 0;foreach record $e2_sll_plan {search [dict get $record tree] {};incr index}
puts "DSP_ACCESS_INSPECTION_FINISHED"
exit 0
