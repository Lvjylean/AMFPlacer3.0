source [file join [file dirname [info script]] dfx_srl_metadata.tcl]
array set mock {empty {} zero 0 one 1 absent {}}
set property_queries 0
proc list_property {cell} {global property_queries; incr property_queries; if {$cell eq "absent"} {return {NAME REF_NAME}}; return {NAME REF_NAME IS_CLK_INVERTED}}
proc get_property {property cell} {global mock; if {$property ne "IS_CLK_INVERTED" || $cell eq "absent"} {error "Unexpected property read"}; return $mock($cell)}
set cases 0
foreach typ {SRL16E SRLC32E} {
    foreach row {{empty {}} {zero 0} {one 1}} {
        lassign $row cell expected
        if {[amf_dfx_srl::inversion $cell $typ] ne $expected} {error "Raw value changed"}
        incr cases
    }
}
if {$property_queries != 2} {error "Primitive property availability was not cached"}
array unset amf_dfx_srl::inversionAvailable
if {[amf_dfx_srl::inversion absent SRL16E] ne "UNKNOWN"} {error "Missing property must remain unknown"}; incr cases
if {![catch {amf_dfx_srl::inversion empty LUT6}]} {error "Unexpected primitive accepted"}; incr cases
puts "PASS cases=$cases raw-empty preserved; explicit 0/1 preserved; absent stays UNKNOWN"
