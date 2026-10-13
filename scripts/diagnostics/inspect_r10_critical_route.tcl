# Read-only routed-DCP diagnosis; never calls route_design or writes a checkpoint.
set_param general.maxThreads 4
lassign $argv input out
file mkdir $out
proc safe_report {filename script} {
    global out
    if {[catch {uplevel 1 $script} error opts]} {
        set f [open [file join $out "$filename.error.txt"] w]
        puts $f [dict get $opts -errorinfo]; close $f
        puts "DIAGNOSTIC_REPORT_ERROR $filename $error"
    }
}
open_checkpoint $input
set sourceName {grp_getrf_core_double_2_512_256_s_fu_1395/s_reg_11314_reg[0]_rep__0/Q}
set sinkName {grp_getrf_core_double_2_512_256_s_fu_1395/grp_subUpdate_double_2_512_s_fu_6074/mul_9ns_2ns_11_1_1_U1576/dout[0]_INST_0_i_14/I3}
set src [get_pins -quiet $sourceName]
set dst [get_pins -quiet $sinkName]
if {[llength $src] != 1 || [llength $dst] != 1} {error "Pins not unique"}
set net [get_nets -quiet -of_objects $src]
set f [open [file join $out identity.tsv] w]
puts $f "role\tpin\tsite\tbel\ttile\tslr\tclock_region\tsite_pins\tnodes"
foreach role {source sink} pin [list $src $dst] {
    set cell [get_cells -of_objects $pin]
    set site [get_sites -of_objects $cell]
    set sp [get_site_pins -quiet -of_objects $pin]
    puts $f [join [list $role $pin $site [get_property BEL $cell] [get_tiles -of_objects $site] [get_slrs -of_objects $site] [get_clock_regions -of_objects $site] $sp [get_nodes -quiet -of_objects $sp]] "\t"]
}
close $f
report_property -all $net -file [file join $out net_properties.rpt]
set endpoints [get_pins -leaf -of_objects $net -filter {DIRECTION == IN}]
set f [open [file join $out sinks.tsv] w]
puts $f "pin\tsite\ttile\tslr\tclock_region"
foreach pin $endpoints {
    set site [get_sites -of_objects [get_cells -of_objects $pin]]
    puts $f [join [list $pin $site [get_tiles -of_objects $site] [get_slrs -of_objects $site] [get_clock_regions -of_objects $site]] "\t"]
}
close $f
set f [open [file join $out pips.tsv] w]
puts $f "pip\tuphill\tdownhill\tdirectional"
foreach pip [get_pips -of_objects $net] {
    puts $f [join [list $pip [get_nodes -uphill -of_objects $pip] [get_nodes -downhill -of_objects $pip] [get_property IS_DIRECTIONAL $pip]] "\t"]
}
close $f
set f [open [file join $out route_nodes.tsv] w]
puts $f "node\ttiles\tslrs"
foreach node [get_nodes -of_objects $net] {
    set tiles [get_tiles -of_objects $node]
    puts $f [join [list $node $tiles [get_slrs -of_objects $tiles]] "\t"]
}
close $f
set paths [get_timing_paths -setup -max_paths 1 -nworst 1]
safe_report path_analysis {report_design_analysis -timing -of_timing_paths $paths -show_all -file [file join $out path_analysis.rpt]}
safe_report congestion {report_design_analysis -congestion -file [file join $out congestion.rpt]}
safe_report net_timing {report_timing -through $src -through $dst -max_paths 1 -input_pins -file [file join $out net_timing.rpt]}
safe_report hold_timing {report_timing -hold -through $src -through $dst -max_paths 1 -input_pins -file [file join $out hold_timing.rpt]}
set f [open [file join $out done.txt] w]
puts $f "Read-only diagnosis complete: [clock format [clock seconds]]";close $f
exit 0
