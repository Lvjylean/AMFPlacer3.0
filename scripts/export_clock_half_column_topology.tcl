# Read the dedicated SLICE clock-leaf graph from an empty target device.
# Usage: vivado -mode batch -source this_file.tcl -tclargs PART NEW_OUT_DIR
#        [CLOCK_REGION ...]
# The 60-row / two 30-row partition is a checked geometry assumption, not a
# queried clock capacity. Leaf/HDISTR counts below are obtained from the graph.
set_param general.maxThreads 4

namespace eval ::amf_clock_topology {
    variable channels {}
    variable leaf_objects [dict create]
    variable exported_leaves [dict create]

    proc text_names {objects} {
        set names {}
        foreach object $objects {lappend names [get_property NAME $object]}
        return [lsort -unique $names]
    }

    proc open_table {out filename header} {
        variable channels
        set channel [open [file join $out $filename] {WRONLY CREAT EXCL}]
        lappend channels $channel
        puts $channel [join $header "\t"]
        return $channel
    }

    proc clock_pins {site} {
        set type [get_property SITE_TYPE $site]
        if {$type ni {SLICEL SLICEM}} {error "Unexpected SLICE type: $site $type"}
        set pins [get_site_pins -of_objects $site -filter {NAME =~ *CLK*}]
        set names {}
        foreach pin $pins {lappend names [file tail [get_property NAME $pin]]}
        set expected {CLK1 CLK2}
        if {$type eq "SLICEM"} {lappend expected LCLK}
        if {[lsort $names] ne [lsort $expected]} {
            error "Unknown clock pin set on $site ($type): $names; expected $expected"
        }
        return $pins
    }

    proc pin_leaves {pin evidence site type cr} {
        variable leaf_objects
        set start [get_nodes -of_objects $pin]
        if {[llength $start] != 1} {error "Expected one connected node for $pin: $start"}
        set frontier $start
        set leaves [dict create]
        for {set depth 1} {$depth <= 2} {incr depth} {
            # All lookups use existing device objects. Never perform a device-wide
            # get_nodes name lookup for each graph vertex.
            set uphill [get_nodes -uphill -of_objects $frontier]
            set next {}
            foreach node $uphill {
                set intent [get_property INTENT_CODE_NAME $node]
                if {$intent eq "NODE_GLOBAL_LEAF"} {
                    set name [get_property NAME $node]
                    dict set leaves $name 1
                    dict set leaf_objects $name $node
                } else {
                    lappend next $node
                }
            }
            if {[llength $next] == 0} {break}
            set frontier $next
        }
        set names [lsort [dict keys $leaves]]
        if {[llength $names] == 0} {error "No dedicated leaf within two hops of $pin"}
        puts $evidence [join [list $site $type $cr $pin \
            [join [text_names $start] ,] [llength $names] [join $names ,]] "\t"]
        return $names
    }

    proc export_leaf_sources {names channel} {
        variable leaf_objects
        variable exported_leaves
        foreach name $names {
            if {[dict exists $exported_leaves $name]} {continue}
            set leaf [dict get $leaf_objects $name]
            set output_pins [get_site_pins -of_objects $leaf]
            set sites [get_sites -of_objects $output_pins]
            if {[llength $sites] != 1} {
                error "Expected one leaf site for $name: $sites"
            }
            set site [lindex $sites 0]
            if {[get_property SITE_TYPE $site] ne "BUFCE_LEAF"} {
                error "Unexpected source site type for $name: $site [get_property SITE_TYPE $site]"
            }
            set input [get_site_pins -of_objects $site -filter {NAME =~ */CLK_IN}]
            if {[llength $input] != 1} {error "Expected one CLK_IN on $site: $input"}
            set input_node [get_nodes -of_objects $input]
            if {[llength $input_node] != 1} {error "Expected one input node on $site: $input_node"}
            set upstream [get_nodes -uphill -of_objects $input_node]
            set hdistr {}
            set other {}
            foreach node $upstream {
                set node_name [get_property NAME $node]
                if {[get_property INTENT_CODE_NAME $node] eq "NODE_GLOBAL_HDISTR"} {
                    lappend hdistr $node_name
                } else {
                    lappend other $node_name
                }
            }
            set hdistr [lsort -unique $hdistr]
            if {[llength $hdistr] == 0} {error "No HDISTR driver found for $name through $input"}
            puts $channel [join [list $name $site $input \
                [join [text_names $input_node] ,] [llength $hdistr] \
                [join $hdistr ,] [join [lsort -unique $other] ,] \
                [join [text_names $output_pins] ,]] "\t"]
            dict set exported_leaves $name 1
        }
    }

    proc run {part out requested_regions metadata} {
        variable exported_leaves
        create_project -in_memory -part $part
        link_design -part $part
        set actual_part [get_property PART [current_design]]
        if {![string equal -nocase $part $actual_part]} {
            error "Part mismatch: requested $part, opened $actual_part"
        }
        puts $metadata "part\t$actual_part"
        puts $metadata "vivado\t[version -short]"
        puts $metadata "half_column_placer_policy_parameter\tplace.maxNumClocksInHalfColumn"
        if {[catch {get_param place.maxNumClocksInHalfColumn} half_policy]} {
            puts $metadata "half_column_placer_policy_value\tunavailable"
        } else {
            puts $metadata "half_column_placer_policy_value\t$half_policy"
        }
        puts $metadata "half_column_placer_policy_interpretation\tread-only Vivado software placement policy; not a physical resource count"

        set all_regions [get_clock_regions]
        set all_region_names [text_names $all_regions]
        if {[llength $requested_regions] == 0} {
            set selected_regions $all_region_names
        } else {
            set selected_regions [lsort -unique $requested_regions]
            foreach cr $selected_regions {
                if {$cr ni $all_region_names} {error "Unknown clock region $cr"}
            }
        }
        puts $metadata "selected_clock_regions\t[join $selected_regions ,]"
        puts $metadata "clock_region_count\t[llength $selected_regions]"
        set selected_set [dict create]
        foreach cr $selected_regions {dict set selected_set $cr 1}

        # Get the site's complete geometry once; the graph scan below only samples
        # each half-column's first and last site and all their clock inputs.
        set columns [dict create]
        set site_types [dict create]
        set slice_sites [get_sites -filter {SITE_TYPE == SLICEL || SITE_TYPE == SLICEM}]
        set slice_names [get_property NAME $slice_sites]
        set slice_regions [get_property CLOCK_REGION $slice_sites]
        set slice_types [get_property SITE_TYPE $slice_sites]
        set site_count [llength $slice_sites]
        foreach values [list $slice_names $slice_regions $slice_types] {
            if {[llength $values] != $site_count} {error "Incomplete batched SLICE properties"}
        }
        foreach site $slice_sites name $slice_names cr $slice_regions type $slice_types {
            if {![dict exists $selected_set $cr]} {continue}
            if {![regexp {^SLICE_X([0-9]+)Y([0-9]+)$} $name -> x y]} {
                error "Unknown SLICE coordinate syntax $name"
            }
            set key [list $cr $x]
            if {[dict exists $columns $key $y]} {error "Duplicate SLICE coordinate $key/$y"}
            dict set columns $key $y $site
            dict set site_types $name $type
        }
        unset slice_sites slice_names slice_regions slice_types
        if {[dict size $columns] == 0} {error "No SLICE columns in selected clock regions"}
        puts $metadata "slice_column_count\t[dict size $columns]"
        flush $metadata

        set halves [open_table $out half_columns.tsv \
            {clock_region slice_x y_min y_max site_types leaf_count leaf_nodes checked_pins}]
        set sources [open_table $out leaf_sources.tsv \
            {leaf_node leaf_site input_pin input_node hdistr_count hdistr_nodes other_upstream_nodes output_pins}]
        set evidence [open_table $out pin_leaf_reachability.tsv \
            {site site_type clock_region pin start_nodes leaf_count leaf_nodes}]
        set count 0
        set half_count 0
        set checked_pin_count 0
        foreach key [lsort -dictionary [dict keys $columns]] {
            lassign $key cr x
            set rows [dict get $columns $key]
            set ys [lsort -integer [dict keys $rows]]
            if {[llength $ys] != 60} {error "Geometry assumption failed: $key has [llength $ys] rows, expected 60"}
            set first [lindex $ys 0]
            for {set i 0} {$i < 60} {incr i} {
                if {[lindex $ys $i] != $first + $i} {
                    error "Geometry assumption failed: $key is not 60 contiguous SLICE rows"
                }
            }
            foreach begin {0 30} {
                set y_min [lindex $ys $begin]
                set y_max [lindex $ys [expr {$begin + 29}]]
                set types {}
                for {set i $begin} {$i < $begin + 30} {incr i} {
                    set site [dict get $rows [lindex $ys $i]]
                    lappend types [dict get $site_types $site]
                }
                set types [lsort -unique $types]
                set reference {}
                set checked {}
                foreach y [list $y_min $y_max] {
                    set site [dict get $rows $y]
                    set type [get_property SITE_TYPE $site]
                    foreach pin [clock_pins $site] {
                        set leaves [pin_leaves $pin $evidence $site $type $cr]
                        if {[llength $reference] == 0} {
                            set reference $leaves
                        } elseif {$leaves ne $reference} {
                            error "Endpoint leaf-set mismatch at $key rows $y_min:$y_max pin $pin"
                        }
                        lappend checked [get_property NAME $pin]
                        incr checked_pin_count
                    }
                }
                export_leaf_sources $reference $sources
                puts $halves [join [list $cr $x $y_min $y_max [join $types ,] \
                    [llength $reference] [join $reference ,] [join [lsort $checked] ,]] "\t"]
                incr half_count
            }
            incr count
            if {$count % 100 == 0} {
                puts "AMF clock topology: $count/[dict size $columns] SLICE columns, $half_count halves, [dict size $exported_leaves] unique leaves"
                flush stdout
                flush $halves
                flush $sources
                flush $evidence
            }
        }
        puts $metadata "half_column_count\t$half_count"
        puts $metadata "checked_pin_count\t$checked_pin_count"
        puts $metadata "unique_leaf_count\t[dict size $exported_leaves]"
        puts "AMF clock topology complete: $count SLICE columns, $half_count halves, [dict size $exported_leaves] unique leaves"
        flush stdout
        close_design
    }
}

if {[llength $argv] < 2} {error "Expected PART NEW_OUT_DIR and optional CLOCK_REGION names"}
set part [lindex $argv 0]
set out [file normalize [lindex $argv 1]]
if {[file exists $out]} {error "Output already exists: $out"}
file mkdir $out
set metadata [open [file join $out metadata.tsv] {WRONLY CREAT EXCL}]
puts $metadata "schema\tamf-clock-half-column-topology-v1"
puts $metadata "scope\tSLICE dedicated clock pin to leaf source and HDISTR connectivity"
puts $metadata "geometry_assumptions\t60 contiguous SLICE rows per CR/x; split into lower and upper 30 rows"
puts $metadata "endpoint_sampling\tfirst and last site of each 30-row half; all CLK1/CLK2 and SLICEM LCLK pins; middle-site connectivity not queried"
puts $metadata "leaf_search\tup to two uphill graph steps; INTENT_CODE_NAME NODE_GLOBAL_LEAF"
puts $metadata "placement_executed\t0"
puts $metadata "routing_executed\t0"
puts $metadata "resource_state\tnominal-empty-device"
puts $metadata "clock_routability_verified\tfalse"
puts $metadata "independent_clock_capacity_verified\tfalse"
puts $metadata "started_utc\t[clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]"
flush $metadata
set started [clock milliseconds]
set code [catch {::amf_clock_topology::run $part $out [lrange $argv 2 end] $metadata} detail options]
foreach channel $::amf_clock_topology::channels {catch {close $channel}}
puts $metadata "elapsed_ms\t[expr {[clock milliseconds] - $started}]"
if {$code == 0} {
    puts $metadata "status\tcompleted"
} else {
    puts $metadata "status\tfailed"
    puts $metadata "error\t[string map [list "\t" { } "\n" { } "\r" { }] $detail]"
    puts stderr $detail
    if {[dict exists $options -errorinfo]} {puts stderr [dict get $options -errorinfo]}
}
close $metadata
exit [expr {$code == 0 ? 0 : 1}]
