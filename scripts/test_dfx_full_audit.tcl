source [file join [file dirname [info script]] dfx_full_audit.tcl]
set dfx(output) [lindex $argv 0]
if {$dfx(output) eq ""} {error "Provide an isolated test output directory"}
file mkdir $dfx(output)
set f [open $dfx(output)/hard_region_sites.tsv w]
puts $f "region\tsite\nstatic\tSLICE_X0Y0\na\tSLICE_X1Y0\nstatic\tRAMB36_X0Y0";close $f
namespace eval amf_dfx {proc primitive_cells {} {global rows;return $rows}}
namespace eval amf_full {
    proc fabric {t} {expr {$t in {LUT6 FDRE RAMB36E2}}}
    proc region {n} {expr {[string match rp/* $n] ? "a" : "static"}}
}
proc get_property {key cells} {
    set i [lsearch -exact {NAME REF_NAME LOC BEL} $key];set values {}
    if {$i<0} {error "Unexpected property: $key"}
    foreach row $cells {lappend values [lindex $row $i]};return $values
}
set baseline {{x LUT6 SLICE_X0Y0 A6LUT} {rp/reg FDRE SLICE_X1Y0 AFF} {ram RAMB36E2 RAMB36_X0Y0 RAMB36E2}}
set targets {x SLICE_X0Y0/A6LUT rp/reg SLICE_X1Y0/AFF ram RAMB36_X0Y0}
set cases 0
proc check {label data shouldFail expectedAdded expectedRemoved} {
    global rows targets cases
    set rows $data;set rc [catch {full_region_audit $targets $label} result]
    if {$rc != $shouldFail} {error "Unexpected audit result: label=$label rc=$rc result=$result"}
    if {!$rc && ([dict get $result vivado_added]!=$expectedAdded || [dict get $result removed_original]!=$expectedRemoved)} {
        error "Wrong netlist-change accounting: $result"
    }
    incr cases
}
check imported $baseline 0 0 0
set added [concat $baseline {{x_replica FDRE SLICE_X0Y0 BFF}}]
check imported $added 1 0 0
check placed $added 0 1 0
set removed [lrange $baseline 1 end]
check imported $removed 1 0 0
check placed $removed 0 0 1
# Equal total size cannot conceal a missing original plus a replica.
check imported [concat $removed {{x_replica FDRE SLICE_X0Y0 BFF}}] 1 0 0
check placed [concat $baseline {{bad_replica FDRE SLICE_X1Y0 BFF}}] 1 0 0
check routed [concat $baseline {{rp/bad_replica FDRE SLICE_X0Y0 BFF}}] 1 0 0
check placed [lreplace $baseline 0 0 {x LUT6 SLICE_X1Y0 A6LUT}] 1 0 0
puts "PASS cases=$cases exact import coverage retained; post-optimization changes recorded; all original/added cells region-checked; site-only RAM accepted"
