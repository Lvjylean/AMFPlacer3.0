source [file join [file dirname [info script]] dfx_static_containment.tcl]
set cases 0
proc expect {code script} {
    global cases
    set rc [catch {uplevel 1 $script} value]
    if {$rc != $code} {error "Expected rc=$code, got rc=$rc: $value"}
    incr cases
}
expect 0 {if {[amf_static::granularity] ne "site"} {error "Legacy default changed"}}
set dfx(static_region_granularity) clock_region
expect 0 {if {[amf_static::granularity] ne "clock_region"} {error "Mode not enabled"}}
set dfx(static_region_granularity) typo
expect 1 {amf_static::granularity}
expect 0 {
    set actual [amf_static::clock_region_ranges {X3Y1 X1Y1 X2Y1 X1Y1}]
    set expected {CLOCKREGION_X1Y1:CLOCKREGION_X1Y1 CLOCKREGION_X2Y1:CLOCKREGION_X2Y1 CLOCKREGION_X3Y1:CLOCKREGION_X3Y1}
    if {$actual ne $expected} {error "CR dedup/order error: $actual"}
}
expect 1 {amf_static::clock_region_ranges {SLICE_X0Y0}}
set grid {CLOCKREGION_X0Y0:CLOCKREGION_X1Y0}
set actualSites {SLICE_X0Y0 SLICE_X1Y0}
set expectedSites $actualSites
proc get_property {key objects} {
    global grid
    if {$key eq "GRID_RANGES"} {return $grid}
    if {$key eq "NAME"} {return $objects}
    error "Unexpected property"
}
proc get_clock_regions {args} {return clock_regions}
proc get_sites {args} {
    global actualSites expectedSites
    if {[lindex $args end] eq "clock_regions"} {return $expectedSites}
    return $actualSites
}
set plan [dict create kept_clock_regions {X0Y0 X1Y0}]
expect 0 {amf_static::validate_clock_region_geometry pblock_static $plan}
set actualSites {SLICE_X0Y0}
expect 1 {amf_static::validate_clock_region_geometry pblock_static $plan}
set actualSites {SLICE_X0Y0 SLICE_X1Y0 SLICE_X2Y0}
expect 1 {amf_static::validate_clock_region_geometry pblock_static $plan}
set actualSites $expectedSites
set grid {CLOCKREGION_X0Y0:CLOCKREGION_X0Y0 SLICE_X1Y0:SLICE_X1Y0}
expect 1 {amf_static::validate_clock_region_geometry pblock_static $plan}
puts "PASS $cases: legacy default, explicit mode, CR syntax, complete membership, and stale/mixed geometry rejection"
