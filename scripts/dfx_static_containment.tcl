# Static placement complement with routing containment. RP geometry is unchanged.
# get_dfx_footprint exposes RP routing tiles, not a static routing guarantee;
# the latter must also be checked on the routed design.
namespace eval amf_static {
    proc enabled {} {
        global dfx
        if {![info exists dfx(static_routing_containment)]} {return 0}
        if {![string is boolean -strict $dfx(static_routing_containment)]} {
            error "Invalid static_routing_containment value"
        }
        return [expr {!!$dfx(static_routing_containment)}]
    }
    proc remove_static_pblock {} {
        global dfx
        set pb [get_pblocks -quiet pblock_static]
        if {![llength $pb]} {return 0}
        foreach row $dfx(rps) {
            set rpPb [get_pblocks [lindex $row 2]]
            if {[get_property PARENT $rpPb] eq "pblock_static"} {
                error "Cannot remove a static Pblock that parents an RP"
            }
        }
        delete_pblocks $pb
        return 1
    }
    proc recover_validate {} {
        if {![enabled]} {
            set removed [remove_static_pblock]
            puts "AMF_STATIC_ROUTING_POLICY containment=0 static_pblock=0 removed=$removed"
            return
        }
        set pb [get_pblocks -quiet pblock_static]
        if {[llength $pb]!=1 || ![get_property CONTAIN_ROUTING $pb] || [get_property IS_SOFT $pb]} {
            error "Invalid recovered static containment"
        }
        if {[granularity] eq "clock_region"} {
            validate_clock_region_geometry $pb [geometry_plan]
        }
    }
    proc routed_audit {} {
        global dfx
        if {![enabled]} {
            set f [open $dfx(output)/static_route_audit_policy.tsv w]
            puts $f "static_routing_containment\tfalse"
            puts $f "static_route_isolation_required\tfalse"
            puts $f "static_route_isolation_verified\tnot_assessed"
            puts $f "targeted_wire_conflict_repair\tnot_implemented"
            close $f
            puts "AMF_STATIC_ROUTE_AUDIT skipped: static routing through RP is permitted"
            return
        }
        set f [open $dfx(output)/static_route_footprint_audit.tsv w]
        puts $f "rp_id\tnet\ttype\tclassification"
        set counts [dict create]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set names [dict create]
            set tiles [get_dfx_footprint -route -of_objects [get_cells $rp]]
            # Bound transient node collections; never enumerate the whole device.
            for {set i 0} {$i<[llength $tiles]} {incr i 64} {
                set nodes [get_nodes -quiet -of_objects [lrange $tiles $i [expr {$i+63}]]]
                foreach net [get_nets -quiet -of_objects $nodes] {dict set names [get_property NAME $net] $net}
            }
            foreach name [lsort [dict keys $names]] {
                set net [dict get $names $name]
                set type [get_property TYPE $net]
                set rpAlias 0
                foreach segment [get_nets -segments $net] {
                    if {[amf_multi::active $segment]} {set rpAlias 1; break}
                }
                if {$type in {GROUND POWER GLOBAL_CLOCK LOCAL_CLOCK REGIONAL_CLOCK}} {
                    set category clock_or_constant
                } elseif {$rpAlias} {
                    set category rp_or_interface
                } else {set category static_only}
                dict incr counts "$id.$category"
                puts $f "$id\t$name\t$type\t$category"
            }
        }
        close $f
        # These exact input nodes caused the earlier B failure. A net owning a
        # multi-tile node does not alone prove an illegal route; preserve details.
        set f [open $dfx(output)/previous_dsp_conflict_nodes.tsv w]
        puts $f "site_pin\tnode\tnets\ttypes"
        foreach suffix {DIN0 DIN3} {
            set pin [get_site_pins DSP48E2_X11Y120/$suffix]
            foreach node [get_nodes -of_objects $pin] {
                set nets [get_nets -quiet -of_objects $node]
                set types {}; foreach n $nets {lappend types [get_property TYPE $n]}
                puts $f "$pin\t$node\t$nets\t$types"
            }
        }
        close $f
        amf_dfx::save_dict $dfx(output)/static_route_audit_counts.tcldata $counts
        puts "AMF_STATIC_ROUTE_AUDIT $counts"
    }
    proc site_ranges {sites} {
        set columns [dict create]
        foreach site $sites {
            if {![regexp {^(.+)_X([0-9]+)Y([0-9]+)$} $site -> kind x y]} {
                error "Cannot express footprint site as a range: $site"
            }
            dict lappend columns "$kind|$x" $y
        }
        set ranges {}
        dict for {column ys} $columns {
            lassign [split $column |] kind x
            set start -1; set last -1
            foreach y [lsort -integer -unique $ys] {
                if {$start<0} {set start $y; set last $y; continue}
                if {$y==$last+1} {set last $y; continue}
                lappend ranges "${kind}_X${x}Y${start}:${kind}_X${x}Y${last}"
                set start $y; set last $y
            }
            if {$start>=0} {lappend ranges "${kind}_X${x}Y${start}:${kind}_X${x}Y${last}"}
        }
        return [lsort $ranges]
    }
    proc granularity {} {
        global dfx
        # Absent means the historical site complement, so old manifests retain
        # their meaning. New clock-region experiments set this explicitly.
        set mode site
        if {[info exists dfx(static_region_granularity)]} {set mode $dfx(static_region_granularity)}
        if {$mode ni {site clock_region}} {error "Invalid static_region_granularity: $mode"}
        return $mode
    }
    proc clock_region_ranges {regions} {
        set result {}
        foreach cr [lsort -dictionary -unique $regions] {
            if {![regexp {^X[0-9]+Y[0-9]+$} $cr]} {error "Invalid clock region: $cr"}
            lappend result "CLOCKREGION_${cr}:CLOCKREGION_${cr}"
        }
        return $result
    }
    proc geometry_plan {{writeEvidence 1}} {
        global dfx
        set mode [granularity]
        set forbidden [dict create]; set routeTiles [dict create]; set excludedCRs {}
        set footprintRows {}; set regionRows {}
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set tiles [get_dfx_footprint -route -of_objects [get_cells $rp]]
            if {![llength $tiles]} {error "Missing routing footprint: $rp"}
            set sites [get_sites -quiet -of_objects $tiles]
            set placementSites [get_sites -of_objects [get_pblocks $pb]]
            foreach tile $tiles {dict set routeTiles $tile 1}
            foreach site [concat $sites $placementSites] {dict set forbidden $site 1}
            # Bulk tile/site queries preserve the actual expanded RP footprint.
            # A tile without sites still contributes its clock region.
            set crs [lsort -dictionary -unique [concat \
                [get_property NAME [get_clock_regions -of_objects $tiles]] \
                [get_property NAME [get_clock_regions -of_objects $placementSites]]]]
            if {![llength $crs]} {error "No clock regions for RP footprint: $rp"}
            set excludedCRs [concat $excludedCRs $crs]
            lappend footprintRows "$id\t[llength [get_dfx_footprint -place -of_objects [get_cells $rp]]]\t[llength $tiles]\t[llength $sites]"
            foreach cr $crs {lappend regionRows "$id\t$cr"}
            if {$writeEvidence} {
                amf_dfx::save_dict $dfx(output)/${id}_route_tiles.tcldata [amf_multi::names $tiles]
            }
        }
        set allCRs [get_property NAME [get_clock_regions]]
        set excludedCRs [lsort -dictionary -unique $excludedCRs]
        set keptCRs {}
        foreach cr $allCRs {if {$cr ni $excludedCRs} {lappend keptCRs $cr}}
        if {$mode eq "clock_region"} {
            if {![llength $keptCRs]} {error "No clock regions remain for static logic"}
            set removeRanges [clock_region_ranges $excludedCRs]
        } else {
            set removeRanges [site_ranges [dict keys $forbidden]]
        }
        if {$writeEvidence} {
            set f [open $dfx(output)/rp_footprints.tsv w]
            puts $f "id\tplace_tiles\troute_tiles\troute_footprint_sites"
            foreach row $footprintRows {puts $f $row}; close $f
            set f [open $dfx(output)/rp_footprint_clock_regions.tsv w]
            puts $f "rp_id\tclock_region"; foreach row $regionRows {puts $f $row}; close $f
            amf_dfx::save_dict $dfx(output)/static_region_plan.tcldata [list \
                granularity $mode all_clock_regions [lsort -dictionary $allCRs] \
                excluded_clock_regions $excludedCRs kept_clock_regions [lsort -dictionary $keptCRs] \
                excluded_footprint_sites [dict size $forbidden]]
        }
        return [dict create mode $mode forbidden $forbidden route_tiles $routeTiles \
            all_ranges [clock_region_ranges $allCRs] remove_ranges $removeRanges \
            excluded_clock_regions $excludedCRs kept_clock_regions $keptCRs]
    }
    proc apply_geometry {pb plan} {
        set current [get_property GRID_RANGES $pb]
        if {[llength $current]} {resize_pblock $pb -remove $current}
        resize_pblock $pb -add [dict get $plan all_ranges]
        if {[llength [dict get $plan remove_ranges]]} {
            resize_pblock $pb -remove [dict get $plan remove_ranges]
        }
        if {[dict get $plan mode] eq "clock_region"} {validate_clock_region_geometry $pb $plan}
    }
    proc validate_clock_region_geometry {pb plan} {
        # Check the site sets as well as the range syntax. Reject a recovered
        # checkpoint with a site-level sliver or a missing whole clock region.
        foreach range [get_property GRID_RANGES $pb] {
            if {![regexp {^CLOCKREGION_X[0-9]+Y[0-9]+:CLOCKREGION_X[0-9]+Y[0-9]+$} $range]} {
                error "Static Pblock is not clock-region-only: $range"
            }
        }
        set expected [lsort [get_property NAME [get_sites -of_objects \
            [get_clock_regions [dict get $plan kept_clock_regions]]]]]
        set actual [lsort [get_property NAME [get_sites -of_objects $pb]]]
        if {$actual ne $expected} {error "Recovered static geometry differs from clock-region footprint complement"}
    }
    proc configure_validate {} {
        global dfx
        if {[version -short] ne "2026.1"} {error "This experiment requires Vivado 2026.1"}
        set clk [get_clocks $dfx(clock_name)]
        if {[llength $clk]!=1 || abs([get_property PERIOD $clk]-$dfx(clock_period_ns))>1e-6} {
            error "Actual clock does not match configured period"
        }
        if {![enabled]} {
            recover_validate
            set f [open $dfx(output)/static_routing_policy.tsv w]
            puts $f "static_routing_containment\tfalse"
            puts $f "static_pblock\tnone"
            puts $f "static_placement_sites\tmodeled_non_prohibited_sites_outside_rp_placement_footprints"
            puts $f "static_route_isolation_required\tfalse"
            puts $f "targeted_wire_conflict_repair\tnot_implemented"
            close $f
            report_drc -checks [get_drc_checks HDPR*] -file $dfx(output)/dfx_preflight_drc.rpt
            set errors [get_drc_violations -quiet -filter {SEVERITY == Error}]
            if {[llength $errors]} {error "DFX preflight DRC errors: $errors"}
            puts "AMF_STATIC_PREFLIGHT routing containment disabled; RP DFX constraints retained"
            return
        }
        set plan [geometry_plan]
        set forbidden [dict get $plan forbidden]; set routeTiles [dict get $plan route_tiles]
        create_pblock pblock_static
        apply_geometry [get_pblocks pblock_static] $plan
        set removeRanges [dict get $plan remove_ranges]
        set_property IS_SOFT false [get_pblocks pblock_static]
        set_property CONTAIN_ROUTING true [get_pblocks pblock_static]
        set filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME != GND && REF_NAME != VCC}
        foreach row $dfx(rps) {append filter " && NAME !~ [lindex $row 1]/*"}
        set staticCells [get_cells -hierarchical -filter $filter]
        if {![llength $staticCells]} {error "Empty static membership"}
        add_cells_to_pblock [get_pblocks pblock_static] $staticCells
        set staticSites [get_sites -of_objects [get_pblocks pblock_static]]
        set overlap {}
        foreach site $staticSites {if {[dict exists $forbidden $site]} {lappend overlap $site}}
        if {[llength $overlap]} {error "Static Pblock overlaps RP footprint sites: [lrange $overlap 0 9]"}
        foreach row $dfx(rps) {
            set rp [lindex $row 1]
            set dynamic [get_cells -hierarchical -filter "IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && NAME =~ $rp/*"]
            set wrong [get_cells -quiet -of_objects [get_pblocks pblock_static] -filter "NAME =~ $rp/*"]
            if {![llength $dynamic] || [llength $wrong]} {error "Invalid static/RP cell ownership: $rp"}
        }
        set f [open $dfx(output)/static_containment.tsv w]
        foreach {k v} [list static_primitives [llength $staticCells] static_sites [llength $staticSites] \
            excluded_sites [dict size $forbidden] rp_route_tiles [dict size $routeTiles] \
            static_rp_site_overlap [llength $overlap] contain_routing [get_property CONTAIN_ROUTING [get_pblocks pblock_static]] \
            is_soft [get_property IS_SOFT [get_pblocks pblock_static]] actual_clock_ns [get_property PERIOD $clk] \
            tool_version [version -short] static_region_granularity [granularity] \
            static_route_isolation_verified false] {puts $f "$k\t$v"}
        close $f
        amf_dfx::save_dict $dfx(output)/static_removed_ranges.tcldata $removeRanges
        amf_dfx::save_dict $dfx(output)/static_derived_ranges.tcldata [get_property GRID_RANGES [get_pblocks pblock_static]]
        report_utilization -pblocks [get_pblocks pblock_static] -file $dfx(output)/static_capacity.rpt
        report_drc -checks [get_drc_checks HDPR*] -file $dfx(output)/containment_preflight_drc.rpt
        set errors [get_drc_violations -quiet -filter {SEVERITY == Error}]
        if {[llength $errors]} {error "DFX preflight DRC errors: $errors"}
        set f [open $dfx(output)/containment_preflight_passed.txt w]
        puts $f "geometry_membership_clock_drc_passed; routed_isolation_pending"; close $f
        puts "AMF_STATIC_PREFLIGHT_PASSED static_cells=[llength $staticCells] excluded_sites=[dict size $forbidden]"
    }
}
