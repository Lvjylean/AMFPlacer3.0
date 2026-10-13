# Read-only device API probe; run in an isolated experiment directory.
create_project -in_memory -part xcu250-figd2104-2L-e
set rtl [open probe.v w]
puts $rtl {module amf_device_probe(input wire a, output wire y); assign y = a; endmodule}
close $rtl
read_verilog probe.v
synth_design -top amf_device_probe -mode out_of_context
puts "AMF_PART=[get_property PART [current_project]]"
puts "AMF_SLRS=[get_slrs]"
foreach pattern {SLICE_X0Y0 DSP48E2_X0Y0 RAMB18_X0Y0 URAM288_X0Y0} {
    set sites [get_sites -quiet $pattern]
    if {[llength $sites] != 1} {error "Expected one site for $pattern: $sites"}
    set site [lindex $sites 0]
    puts "AMF_SITE=$site"
    puts "AMF_PROPERTIES=[list_property $site]"
    puts "AMF_SITE_TYPE=[get_property SITE_TYPE $site]"
    puts "AMF_RPM=[get_property RPM_X $site],[get_property RPM_Y $site]"
    puts "AMF_CR=[get_property CLOCK_REGION $site]"
    puts "AMF_SLR=[get_slrs -of_objects $site]"
    set tile [get_tiles -of_objects $site]
    puts "AMF_TILE=$tile"
    puts "AMF_TILE_PROPERTIES=[list_property $tile]"
    puts "AMF_BELS=[get_bels -of_objects $site]"
}
foreach name {SLICE_X0Y1 SLICE_X0Y239 SLICE_X0Y240 DSP48E2_X0Y1 DSP48E2_X0Y47 DSP48E2_X0Y48 URAM288_X0Y1 URAM288_X0Y15 URAM288_X0Y16} {
    set s [get_sites $name]
    puts "AMF_COORD $name [get_property RPM_X $s] [get_property RPM_Y $s] [get_property CLOCK_REGION $s] [get_slrs -of_objects $s]"
}
foreach pattern {SLICE_* DSP48E2_* RAMB18_* RAMB36_* URAM288_*} {
    puts "AMF_SITE_COUNT $pattern [llength [get_sites -quiet $pattern]]"
}
puts "AMF_PROBE_OK"
close_project
exit 0
