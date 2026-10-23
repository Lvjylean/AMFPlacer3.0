source [file join [file dirname [info script]] .. dfx_native_lut_pins.tcl]
set path [file join /tmp amf74-lut-table-[pid].tsv]
proc table {rows} {
    set f [open $::path w];puts $f "cell\tsite\tbel\tlogical_pin\tphysical_pin"
    foreach r $rows {puts $f [join $r \t]};close $f
}
table {{rp/c SLICE_X0Y0 A6LUT I0 A2} {rp/c SLICE_X0Y0 A6LUT I1 A1}}
set p [amf_native_lut_pins::read_plan $path rp/]
if {[dict get $p c pins I0] ne "A2"} {error "Mapping not preserved"}
table {{rp/c SLICE_X0Y0 A6LUT I0 A2} {rp/c SLICE_X0Y0 A6LUT I1 A2}}
if {![catch {amf_native_lut_pins::read_plan $path rp/}]} {error "Duplicate physical input accepted"}
table {{rp/c SLICE_X0Y0 A5LUT I0 A6}}
if {![catch {amf_native_lut_pins::read_plan $path rp/}]} {error "Invalid 5LUT A6 accepted"}
table {{other/c SLICE_X0Y0 A6LUT I0 A2}}
if {![catch {amf_native_lut_pins::read_plan $path rp/}]} {error "Escaped RP accepted"}
file delete $path
puts "LUT pin import schema and identity tests passed"
