# Invoked only after both independent verification branches passed.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_clock_contract.tcl]
proc main {} {
 global dfx
 set_param general.maxThreads $dfx(vivado_threads)
 amf_dfx::timed open_verified_routed {open_checkpoint $dfx(routed_checkpoint)}
 dfx_clock_contract::assert_top bitstream_reopen 1
 # Establish the RP -> Pblock -> automatic partial filename mapping natively.
 set names {}; set rows {}
 foreach row $dfx(rps) {
  lassign $row id rp pb
  set cell [get_cells -quiet $rp]
  if {[llength $cell]!=1 || ![get_property HD.RECONFIGURABLE $cell]} {error "Invalid RP for partial bitstream: $rp"}
  set actual [get_pblocks -of_objects $cell]
  if {[llength $actual]!=1 || [get_property NAME $actual] ne $pb} {error "Unexpected Pblock mapping: $rp"}
  set file $dfx(output)/full_[set pb]_partial.bit
  if {[dict exists $names $file]} {error "Duplicate automatic partial path"}
  dict set names $file 1
  lappend rows [list $id $rp $pb $file]
 }
 # Vivado writes full.bit and each RP partial in this single invocation.
 amf_dfx::timed full_and_partial_bitstreams {write_bitstream $dfx(output)/full.bit}
 set f [open $dfx(output)/partial_mapping.tsv w]
 foreach row $rows {
  lassign $row id rp pb file
  if {![file exists $file] || [file size $file]==0} {error "Missing automatic partial: $file"}
  puts $f [join $row \t]
 }
 close $f
 puts AMF_VERIFIED_BITSTREAMS_COMPLETED
}
if {[catch {main} msg opts]} {puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2}
exit 0
