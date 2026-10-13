set out [lindex $argv 0]
open_checkpoint [lindex $argv 1]
set f [open $out w]; puts $f "cr\tslr\tsite_type\tcount"
foreach cr {X2Y1 X2Y5} {
 set obj [get_clock_regions $cr]
 if {[llength $obj]!=1} {error "Missing clock region $cr"}
 set counts [dict create]; set sites [get_sites -of_objects $obj]
 if {![llength $sites]} {error "Empty region $cr"}
 foreach s $sites {dict incr counts [get_property SITE_TYPE $s]}
 foreach t [lsort [dict keys $counts]] {puts $f "$cr\t[get_slrs -of_objects $obj]\t$t\t[dict get $counts $t]"}
}
close $f
exit 0

