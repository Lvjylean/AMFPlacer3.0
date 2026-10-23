# Read-only compatibility preflight: open the R10 input and verify its timing target.
if {$argc != 4} {error "Expected input.dcp output-directory expected-part expected-period-ns"}
lassign $argv input out expectedPart expectedPeriod
file mkdir $out
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set part [get_property PART [current_design]]
    if {$part ne $expectedPart} {error "Unexpected part: $part"}
    set clocks [get_clocks -quiet *]
    if {[llength $clocks] != 1 || [get_property NAME [lindex $clocks 0]] ne "ap_clk"} {
        error "Expected only the R10 ap_clk clock"
    }
    set period [get_property PERIOD [lindex $clocks 0]]
    if {abs($period-$expectedPeriod) > 0.000001} {error "Input clock period differs from requested target"}
    report_clocks -file [file join $out clocks.rpt]
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    set f [open [file join $out preflight.json] w]
    puts $f [format {{"vivado_version":"%s","part":"%s","clock":"ap_clk","period_ns":%s,"primitive_cells":%d,"input_checkpoint_opened":true,"placement_executed":false,"routing_executed":false}} [version -short] $part $period [llength $cells]]
    close $f
    puts "AMF_VIVADO_VERSION_PREFLIGHT_PASSED"
} failure options]} {
    puts stderr $failure
    puts stderr [dict get $options -errorinfo]
    exit 2
}
exit 0
