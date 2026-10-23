source [file join [file dirname [info script]] .. dfx_clock_guidance.tcl]
proc require {value message} {if {!$value} {error $message}}
set query_calls 0
proc get_cells {args} {incr ::query_calls;return {c0 c1}}
proc get_nets {args} {return {clk_net}}
proc get_property {key objects} {
    switch -- $key {
        NAME {return $objects}
        LOC {return [list SLICE_X0Y0 [expr {$::moved ? "SLICE_X1Y0" : "SLICE_X0Y0"}]]}
        BEL {return {AFF BFF}}
        IS_ROUTE_FIXED {return 1}
        FIXED_ROUTE {return {ROOT {BRANCH END}}}
        default {error "Unexpected property $key"}
    }
}
namespace eval amf_dfx {
    proc save_dict {path value} {set f [open $path w];puts $f $value;close $f}
    proc timed {label body} {lappend ::stages $label;uplevel 1 $body}
}
namespace eval dfx_clock_contract {
    proc enabled {} {return 1}
    proc signature {} {return {dfx_clk {jtag/TCK 12 0}}}
    proc assert_top {args} {}
    proc assert_unchanged {signature label} {if {$signature ne [signature]} {error changed}}
}
proc update_clock_routing {args} {
    require [expr {$args eq "-verbose"}] "Missing detailed guidance log"
    incr ::updates
    if {$::move_on_update} {set ::moved 1}
}
set moved 0;set move_on_update 0;set updates 0;set stages {}
set dfx(output) [file join /tmp amf74-clock-test-[pid]]
file mkdir $dfx(output)
dfx_clock_guidance::apply
require [expr {$query_calls==0 && $updates==0}] "Disabled mode changes ordinary behavior"
set dfx(clock_guidance_mode) update-preserve
set dfx(backend_place_mode) route-only
dfx_clock_guidance::apply
require [expr {$updates==1 && [llength $stages]==5}] "Expected exactly one timed clock update and separate complete checks"
require [file exists $dfx(output)/clock_guidance_update.tsv] "Missing preservation evidence"
set move_on_update 1
require [catch {dfx_clock_guidance::apply} error] "Cell movement was not rejected"
require [string match {*Full design placement changed*} $error] "Wrong preservation failure"
set dfx(clock_guidance_mode) unknown
require [catch {dfx_clock_guidance::apply}] "Unknown policy accepted"
file delete -force $dfx(output)
puts "clock guidance opt-in and preservation tests passed"
