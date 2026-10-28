set out [file normalize [lindex $argv 0]]
proc main {} {
 global out
 set_param general.maxThreads 4
 read_verilog $out/top.v
 synth_design -top top -part xcu250-figd2104-2L-e -flatten_hierarchy none
 create_clock -name clk -period 10 [get_ports clk]
 set_input_delay -clock clk 1 [get_ports {d[*]}]
 set_output_delay -clock clk 1 [get_ports {q[*]}]
 foreach rp {a b} xlo {0 5} xhi {2 7} {
  set_property HD.RECONFIGURABLE true [get_cells $rp]
  create_pblock pb_$rp
  add_cells_to_pblock [get_pblocks pb_$rp] [get_cells $rp]
  resize_pblock [get_pblocks pb_$rp] -add CLOCKREGION_X${xlo}Y4:CLOCKREGION_X${xhi}Y11
  set_property SNAPPING_MODE ON [get_pblocks pb_$rp]
  set_property RESET_AFTER_RECONFIG true [get_pblocks pb_$rp]
  set_property IS_SOFT false [get_pblocks pb_$rp]
 }
 opt_design
 foreach rp {a b} cx {1 6} {
  foreach name {source local_ff remote} cy {5 5 9} offset {20 24 20} {
   set sites [lsort -dictionary [get_sites -of_objects [get_clock_regions X${cx}Y${cy}] -filter {SITE_TYPE =~ SLIC*}]]
   if {[llength $sites]<=$offset} {error "Missing fixture SLICE sites"}
   set site [lindex $sites $offset]
   set_property LOC $site [get_cells $rp/$name]
   set_property BEL AFF [get_cells $rp/$name]
  }
 }
 foreach name {static_source static_sink} cy {5 9} {
  set sites [lsort -dictionary [get_sites -of_objects [get_clock_regions X3Y$cy] -filter {SITE_TYPE =~ SLIC*}]]
  if {[llength $sites]<=20} {error "Missing static fixture SLICE sites"}
  set_property LOC [lindex $sites 20] [get_cells $name]
  set_property BEL AFF [get_cells $name]
 }
 set expected {}
 foreach c [get_cells -hierarchical -filter {REF_NAME == FDRE}] {
  dict set expected [get_property NAME $c] [get_property LOC $c]
  set_property IS_LOC_FIXED true $c
  set_property IS_BEL_FIXED true $c
 }
 place_design
 dict for {name site} $expected {
  if {[get_property LOC [get_cells $name]] ne $site} {error "Fixture placement changed: $name"}
 }
 foreach rp {a b} {
  set src [get_slrs -of_objects [get_sites [get_property LOC [get_cells $rp/source]]]]
  set dst [get_slrs -of_objects [get_sites [get_property LOC [get_cells $rp/remote]]]]
  if {$src eq $dst} {error "Fixture is not cross-die: $rp"}
 }
 foreach rp {a b} {
  write_checkpoint -cell $rp $out/${rp}_placed.dcp
  set f [open $out/${rp}-cells.tsv w];puts $f "cell\tsite"
  foreach c [get_cells -hierarchical -filter "IS_PRIMITIVE && NAME =~ $rp/*"] {
   if {[get_property REF_NAME $c] ni {GND VCC}} {puts $f "[get_property NAME $c]\t[get_property LOC $c]"}
  };close $f
  set f [open $out/${rp}-allowed.txt w]
  puts $f [join [get_property NAME [get_sites -of_objects [get_pblocks pb_$rp]]] "\n"];close $f
 }
 write_checkpoint $out/base_placed.dcp
 route_design
 report_route_status -file $out/base-route.rpt
 report_timing_summary -file $out/base-timing.rpt
 write_checkpoint $out/base_routed.dcp
 # Preserve interface and clock route stubs in each module checkpoint.
 foreach rp {a b} {
  set internal [get_nets -of_objects [get_pins $rp/source/Q]]
  route_design -unroute -nets $internal
  write_checkpoint -force -cell $rp $out/${rp}_placed.dcp
 }
 foreach rp {a b} {update_design -cell $rp -black_box}
 lock_design -level routing
 write_checkpoint $out/common_static_locked.dcp
 puts E2_SYNTHETIC_DFX_BASE_PASS
 close_design
}
if {[catch {main} message options]} {puts stderr $message;puts stderr [dict get $options -errorinfo];exit 2}
exit 0
