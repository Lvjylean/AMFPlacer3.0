namespace eval e2_sll {}
proc e2_sll::usage {out} {
 set f [open [file join $out sll-used.tsv] w]
 puts $f "net\tnode\tboundary\tphysical_column\tendpoint_tiles"
 set used {};set owners {}
 try {
 foreach pair {{SLR0 SLR1} {SLR1 SLR2} {SLR2 SLR3}} {
  foreach node [get_nodes -slls_between [get_slrs $pair] -filter {IS_USED == 1}] {
   set name [get_property NAME $node]
   if {[dict exists $used $name]} {error "Repeated physical SLL $name"}
   dict set used $name 1
   set tiles [get_tiles -of_objects $node]
   set columns [lsort -unique [get_property COLUMN $tiles]]
   if {[llength $columns]!=1} {error "SLL column mapping mismatch $name"}
   set nets [get_property NAME [get_nets -quiet -of_objects $node]]
   if {[llength $nets]!=1} {error "SLL owner mismatch $name: $nets"}
   dict set owners [lindex $nets 0] 1
   puts $f [join [list [lindex $nets 0] $name [join $pair :] [lindex $columns 0] [join $tiles ,]] "\t"]
  }
 }
 } finally {close $f}
 puts "E2_SLL_INVENTORY unique_nodes=[dict size $used] nets=[dict size $owners]"
}
