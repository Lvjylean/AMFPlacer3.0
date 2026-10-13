# Query the software placement budget independently of physical leaf topology.
set_param general.maxThreads 4
lassign $argv part out
if {[llength $argv] != 2 || [file exists $out]} {error "Expected PART NEW_OUT"}
file mkdir $out
set f [open $out/clock_policy.tsv w]
puts $f "stage\tparameter\tvalue"
set p place.maxNumClocksInHalfColumn
puts $f "before_device\t$p\t[get_param $p]"
create_project -in_memory -part $part
link_design -part $part
puts $f "after_link_design\t$p\t[get_param $p]"
close $f
set f [open $out/metadata.tsv w]
puts $f "part\t[get_property PART [current_design]]\nvivado\t[version -short]\nparameter_modified\t0\nplacement_executed\t0\nresource_scope\tsoftware-placement-policy-not-physical-capacity"
close $f
close_design
exit 0
