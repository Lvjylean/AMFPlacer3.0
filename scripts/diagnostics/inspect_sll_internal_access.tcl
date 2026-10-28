set out [file normalize [lindex $argv 0]];file mkdir $out
set_param general.maxThreads 2
open_checkpoint [lindex $argv 1]
set f [open [lindex $argv 2] r];set wanted [string trim [read $f]];close $f
source [lindex $argv 3]
set recordIndex -1
proc find_receiver {items wanted} {
 foreach child $items {
  if {[dict exists $child pin]} {if {[dict get $child pin] eq $wanted} {return 1}} elseif {[find_receiver [dict get $child children] $wanted]} {return 1}
 }
 return 0
}
set index 0
foreach record $e2_sll_plan {if {[find_receiver [dict get $record tree] $wanted]} {set recordIndex $index;break};incr index}
if {$recordIndex<0} {error "Receiver not in plan"}
set f [open $out/mapping.txt w]
puts $f [list record_index $recordIndex record $record]
foreach name [list [dict get $record driver] $wanted] {
 set cell [get_cells [file dirname $name]]
 set pin [get_pins -of_objects $cell -filter "NAME == $name"]
 puts $f [list pin $pin cell $cell loc [get_property LOC $cell] bel [get_property BEL $cell] leaf [get_property IS_LEAF $pin]]
 set bp [get_bel_pins -of_objects $pin]
 puts $f [list bel_pins $bp site_pins [get_site_pins -quiet -of_objects $pin]]
 foreach b $bp {
  puts $f [list bel $b uphill [get_bel_pins -uphill -of_objects $b] uphill_net [get_bel_pins -uphill -of_objects $b -filter {IS_USED}]]
  report_property -all -file $out/[file tail $name]-bel.txt $b
 }
 set net [get_nets -parent_net -segments -of_objects $pin]
 puts $f [list net $net belpins [get_bel_pins -quiet -of_objects $net] sitepins [get_site_pins -quiet -of_objects $net]]
 report_property -all -file $out/[file tail $name]-pin.txt $pin
}
close $f
puts "INTERNAL_ACCESS_INSPECTION_FINISHED"
exit 0
