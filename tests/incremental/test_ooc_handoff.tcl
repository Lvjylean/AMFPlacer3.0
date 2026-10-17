source [lindex $argv 0]
set dfx(clock_name) dfx_clk
set dfx(clock_period_ns) 12
set dfx(rm_clock_port) ap_clk
set dfx(output) [lindex $argv 1]
set mock_clock 1
set mock_extra {set_false_path -from [get_ports ap_rst_n]}
set reset_count 0
proc get_clocks {args} {global mock_clock; if {$mock_clock} {return dfx_clk};return {}}
proc get_property {p x} {
 switch -- $p {IS_GENERATED {return 0} SOURCE_PINS {return ap_clk} PERIOD {return 12} default {error "Unexpected property"}}
}
proc write_xdc {args} {
 global mock_clock mock_extra
 set f [open [lindex $args end] w]
 if {$mock_clock} {
  puts $f {create_clock -name dfx_clk -period 12 [get_ports ap_clk]}
  if {$mock_extra ne ""} {puts $f $mock_extra}
 }
 puts $f {current_instance -quiet}
 close $f
}
proc reset_timing {} {global mock_clock reset_count;set mock_clock 0;incr reset_count}
if {![catch {amf_ooc_clock::strip_checked_primary} msg]} {error "Additional timing constraint silently removed"}
if {$reset_count!=0} {error "Reset ran before rejection"}
puts "ADDITIONAL_CONSTRAINT_REJECTED_BEFORE_RESET $msg"
set mock_extra {}
amf_ooc_clock::strip_checked_primary
if {$mock_clock || $reset_count!=1} {error "Known single primary was not removed exactly once"}
puts SINGLE_PRIMARY_ONLY_REMOVED

