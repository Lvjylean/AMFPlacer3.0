# Opt-in route-only clock handoff. Do not alter ordinary/full flow behavior.
namespace eval dfx_clock_guidance {
    variable comparator [file join [file dirname [info script]] dfx_compare_placement.py]
    variable route_comparator [file join [file dirname [info script]] dfx_compare_clock_route.py]
    variable serial 0
    proc enabled {} {
        global dfx
        if {![info exists dfx(clock_guidance_mode)]} {return 0}
        if {$dfx(clock_guidance_mode) ni {off update-preserve}} {error "Unknown clock guidance mode"}
        return [expr {$dfx(clock_guidance_mode) eq "update-preserve"}]
    }
    proc placement {} {
        global dfx
        variable serial
        set prefix $dfx(output)/clock_placement_[incr serial]
        set cells [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}]
        set names [get_property NAME $cells]
        set locs [get_property LOC $cells]
        set bels [get_property BEL $cells]
        if {[llength $cells]!=[llength $names] || [llength $cells]!=[llength $locs] || [llength $cells]!=[llength $bels]} {
            error "Incomplete placement snapshot for clock update"
        }
        # Batch vectors avoid slow per-cell Tcl dictionary operations. Python
        # checks the complete inventory, duplicates, and every LOC/BEL pair.
        foreach suffix {names locs bels} values [list $names $locs $bels] {
            set f [open $prefix.$suffix w]
            fconfigure $f -encoding utf-8 -translation lf
            puts $f [join $values \n]
            close $f
        }
        set f [open $prefix.count w];puts $f [llength $cells];close $f
        puts "AMF_CLOCK_PLACEMENT_SNAPSHOT prefix=$prefix cells=[llength $cells]"
        return [list $prefix [llength $cells]]
    }
    proc fixed_clocks {} {
        set result {}
        # Restrict the cheap TYPE query first; evaluate route-fixed properties
        # only on clock nets, not on every signal net in the full design.
        # A hierarchical net has many logical segments but one physical route.
        # Select every canonical top segment, not one hardcoded buffer/net name.
        set clocks [get_nets -hierarchical -top_net_of_hierarchical_group -filter {TYPE == GLOBAL_CLOCK || TYPE == CLOCK}]
        puts "AMF_CLOCK_FIXED_ENUMERATE clock_candidates=[llength $clocks]"
        if {![llength $clocks]} {error "Missing canonical clock nets"}
        set names [get_property NAME $clocks]
        set locks [get_property IS_ROUTE_FIXED $clocks]
        if {[llength $names]!=[llength $clocks] || [llength $locks]!=[llength $clocks]} {error "Incomplete clock property vectors"}
        foreach net $clocks name $names locked $locks {
            if {!$locked} {continue}
            if {[dict exists $result $name]} {error "Duplicate canonical clock net"}
            dict set result $name [get_property FIXED_ROUTE $net]
        }
        if {![dict size $result]} {error "Expected fixed static clock routing"}
        puts "AMF_CLOCK_FIXED_SNAPSHOT fixed_clocks=[dict size $result]"
        return $result
    }
    proc exact {before after what} {
        global dfx
        variable route_comparator
        if {[dict size $before]!=[dict size $after]} {error "$what inventory changed during clock update"}
        if {![file isfile $route_comparator]} {error "Missing complete route topology comparator"}
        set directory $dfx(output)/clock_route_comparison
        file mkdir $directory
        set index 0
        dict for {name value} $before {
            if {![dict exists $after $name]} {
                error "$what changed during clock update: $name"
            }
            # FIXED_ROUTE serialization may reorder sibling branches. Preserve
            # every node, directed edge, root and occurrence, not just a set of
            # nodes. Comparing per canonical net also preserves net ownership.
            set prefix $directory/[incr index]
            foreach suffix {before after} route [list $value [dict get $after $name]] {
                set f [open $prefix.$suffix w]
                fconfigure $f -encoding utf-8 -translation lf
                puts $f $route
                close $f
            }
            set f [open $prefix.net w];puts $f $name;close $f
            puts [exec python3 $route_comparator $prefix.before $prefix.after $prefix.json]
        }
    }
    proc apply {} {
        global dfx
        variable comparator
        if {![enabled]} {return}
        if {$dfx(backend_place_mode) ne "route-only" || ![dfx_clock_contract::enabled]} {
            error "Clock guidance update requires route-only and explicit clock contract"
        }
        dfx_clock_contract::assert_top before_clock_guidance 1
        set signature [dfx_clock_contract::signature]
        amf_dfx::timed clock_guidance_placement_snapshot {set before [placement]}
        amf_dfx::timed clock_guidance_fixed_snapshot {set fixed [fixed_clocks]}
        amf_dfx::save_dict $dfx(output)/clock_guidance_fixed_before.tcldata $fixed
        if {![file isfile $comparator]} {error "Missing complete placement comparator"}
        amf_dfx::timed update_clock_routing {update_clock_routing -verbose}
        amf_dfx::timed clock_guidance_placement_verify {
            set after [placement]
            puts [exec python3 $comparator [lindex $before 0] [lindex $after 0] $dfx(output)/clock_guidance]
        }
        amf_dfx::timed clock_guidance_fixed_verify {
            set fixed_after [fixed_clocks]
            amf_dfx::save_dict $dfx(output)/clock_guidance_fixed_after.tcldata $fixed_after
            exact $fixed $fixed_after "Fixed clock route"
            dfx_clock_contract::assert_unchanged $signature after_clock_guidance
            dfx_clock_contract::assert_top after_clock_guidance 1
        }
        set f [open $dfx(output)/clock_guidance_update.tsv w]
        puts $f "mode\tupdate-preserve\nplacement_cells\t[lindex $before 1]\nplacement_changes\t0\nfixed_clock_nets\t[dict size $fixed]\nfixed_clock_route_changes\t0\nplace_design_calls\t0\nexplicit_unroute_calls\t0"
        close $f
        puts "AMF_CLOCK_GUIDANCE_UPDATE_PRESERVED cells=[lindex $before 1] fixed_clocks=[dict size $fixed]"
    }
}
