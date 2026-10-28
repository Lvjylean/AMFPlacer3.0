# Source with auditPaths already obtained from the unchanged routed checkpoint.
set out $auditOutput
set f [open $out/actual-top10000.tsv w]
puts $f "rank\tstartpoint\tendpoint\tdelay_ns\tslack_ns\trequirement_ns\tpins\tdirections"
set rank 0
foreach p $auditPaths {
 incr rank
 set pins [get_pins -quiet -of_objects $p]
 puts $f [join [list $rank [get_property STARTPOINT_PIN $p] [get_property ENDPOINT_PIN $p] [get_property DATAPATH_DELAY $p] [get_property SLACK $p] [get_property REQUIREMENT $p] [join [get_property NAME $pins] |] [join [get_property DIRECTION $pins] |]] "\t"]
 if {$rank%1000==0} {flush $f;puts "ACTUAL_EXPORTED $rank"}
}
close $f
puts "ACTUAL_TOP10000_COMPLETE $rank"
