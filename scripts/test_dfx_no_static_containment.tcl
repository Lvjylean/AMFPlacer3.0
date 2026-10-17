source [file join [file dirname [info script]] dfx_static_containment.tcl]
source [file join [file dirname [info script]] dfx_static_membership.tcl]
source [file join [file dirname [info script]] dfx_full_input.tcl]
set tests 0
proc expect {condition} {
    if {![uplevel 1 [list expr $condition]]} {error "Assertion failed: $condition"}
    incr ::tests
}
set out [lindex $argv 0]
file mkdir $out
array set dfx [list output $out clock_name clk clock_period_ns 20 modeled_sites $out/modeled.txt \
    rps {{a rp_a pblock_a {} SLR0} {b rp_b pblock_b {} SLR1}}]
expect {![amf_static::enabled]}
set dfx(static_routing_containment) false
expect {![amf_static::enabled]}
set dfx(static_routing_containment) typo
expect {[catch {amf_static::enabled}]}
set dfx(static_routing_containment) 0

# The old static Pblock is removed; RP constraints stay in their own Pblocks.
set haveStatic 1
set removed 0
proc get_pblocks {args} {
    set name [lindex $args end]
    if {$name eq "pblock_static" && !$::haveStatic} {return {}}
    return $name
}
proc delete_pblocks {name} {
    if {$name ne "pblock_static"} {error "Attempt to delete RP Pblock"}
    set ::haveStatic 0;incr ::removed
}
proc get_property {property objects} {
    if {$property eq "PARENT"} {return {}}
    if {$property eq "PERIOD"} {return 20}
    if {$property eq "NAME"} {return $objects}
    error "Unexpected property query: $property $objects"
}
amf_static::recover_validate
expect {!$haveStatic && $removed==1}
amf_static::recover_validate
expect {$removed==1}

# A static site in the expanded routing footprint remains available. The
# separate placement-only halo and the two RP-owned sites must be excluded.
proc get_cells {args} {return [lindex $args end]}
proc get_dfx_footprint {args} {
    if {[lindex $args 0] ne "-place"} {error "Routing footprint must not reserve static sites"}
    return [expr {[lindex $args end] eq "rp_a" ? "tile_a" : "tile_b"}]
}
proc get_sites {args} {
    set i [lsearch -exact $args -of_objects]
    if {$i>=0} {
        set object [lindex $args [expr {$i+1}]]
        switch $object {
            pblock_a {return A}
            pblock_b {return B}
            tile_a {return {A PLACEMENT_HALO}}
            tile_b {return B}
            pblock_static {return {STATIC_1 STATIC_2}}
            default {error "Unexpected site source: $object"}
        }
    }
    # PROHIBITED is intentionally absent from the non-prohibited API result.
    return {STATIC_1 STATIC_2 ROUTING_ONLY A B PLACEMENT_HALO UNMODELED}
}
set f [open $dfx(modeled_sites) w]
puts $f "STATIC_1\nSTATIC_2\nROUTING_ONLY\nA\nB\nPLACEMENT_HALO\nPROHIBITED"
close $f
expect {[amf_full::export_regions $out]==5}
set f [open $out/hard_region_sites.tsv r];set rows [read $f];close $f
expect {[string first "static\tROUTING_ONLY\n" $rows]>=0}
expect {[string first "static\tA\n" $rows]<0}
expect {[string first "static\tB\n" $rows]<0}
expect {[string first "PLACEMENT_HALO" $rows]<0}
expect {[string first "PROHIBITED" $rows]<0}
expect {[string first "UNMODELED" $rows]<0}
expect {[string first "a\tA\n" $rows]>=0 && [string first "b\tB\n" $rows]>=0}

# Disabled containment skips the expensive membership and wire census, rather
# than treating the absence of a static Pblock as a correctness failure.
rename get_cells get_cells_mock
proc get_cells {args} {error "Unexpected whole-static cell query"}
set summary [amf_static_membership::enforce pre_route 1]
expect {[dict get $summary enabled]==0 && [dict get $summary state] eq "not_applicable"}
amf_static::routed_audit
expect {[file exists $out/static_route_audit_policy.tsv]}
expect {![file exists $out/static_route_footprint_audit.tsv]}
proc version {args} {return 2026.1}
proc get_clocks {args} {return clk}
proc get_drc_checks {args} {return HDPR}
proc report_drc {args} {set ::drcCalled 1}
proc get_drc_violations {args} {return {}}
set drcCalled 0
amf_static::configure_validate
expect {$drcCalled && !$haveStatic}
expect {[file exists $out/static_routing_policy.tsv]}

# Explicit opt-in still selects the old placement mask without querying route
# footprints. This protects historical containment regression coverage.
set dfx(static_routing_containment) 1
set haveStatic 1
expect {[amf_static::enabled]}
expect {[amf_full::export_regions $out]==4}
set f [open $out/hard_region_sites.tsv r];set rows [read $f];close $f
expect {[string first "ROUTING_ONLY" $rows]<0}
puts "PASS $tests no-static-containment policy and resource-mask checks"
