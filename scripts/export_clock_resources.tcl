# Raw, read-only clock-device evidence for export_fabric_device.tcl.
# No place_design/route_design and no capacities inferred from clock-site counts.
# Nominal architecture capacities are resolved separately from this evidence.
namespace eval amf_clock_resources {}

proc amf_clock_resources::field {value} {
    return [string map [list "\\" "\\\\" "\t" "\\t" "\r" "\\r" "\n" "\\n"] $value]
}

proc amf_clock_resources::row {channel values} {
    set escaped {}
    foreach value $values {lappend escaped [field $value]}
    puts $channel [join $escaped "\t"]
}

# Discover exposed properties first: property names differ across architectures
# and tool versions. An absent property must remain unavailable, never zero.
proc amf_clock_resources::properties {output queries object} {
    if {[catch {set names [lsort [list_property $object]]} message]} {
        row $queries [list "list_property:$object" unavailable $message]
        return [dict create]
    }
    set values [dict create]
    foreach name $names {
        if {[catch {set value [get_property $name $object]} message]} {
            row $output [list $object $name unavailable $message]
        } else {
            dict set values $name $value
            row $output [list $object $name ok $value]
        }
    }
    row $queries [list "list_property:$object" ok [llength $names]]
    return $values
}

proc amf_clock_resources::export {out partMode crSlr} {
    set queries [open $out/clock_resource_queries.tsv w]
    row $queries {query status detail}
    set partName [get_property PART [current_design]]
    set parts [get_parts -quiet $partName]
    if {[llength $parts] != 1} {error "Expected one exact part object for $partName"}

    set output [open $out/clock_part_properties.tsv w]
    row $output {object property status value}
    set partValues [properties $output $queries [lindex $parts 0]]
    close $output

    set output [open $out/clock_design_properties.tsv w]
    row $output {object property status value}
    properties $output $queries [current_design]
    close $output

    set output [open $out/clock_region_properties.tsv w]
    row $output {object property status value}
    foreach cr [get_clock_regions] {properties $output $queries $cr}
    close $output

    # --clock-resources works without the larger --physical export.
    if {![file exists $out/clock_regions.tsv]} {
        set output [open $out/clock_regions.tsv w]
        row $output {clock_region slr}
        dict for {cr slr} $crSlr {row $output [list $cr $slr]}
        close $output
    }

    set output [open $out/clock_sites.tsv w]
    row $output {site site_type clock_region slr tile prohibited}
    set clockSites [get_sites -quiet -filter {SITE_TYPE =~ BUFG* || SITE_TYPE =~ BUFCE* || SITE_TYPE =~ BUFH* || SITE_TYPE =~ BUFR* || SITE_TYPE =~ BUFIO* || SITE_TYPE =~ BUFMR* || SITE_TYPE =~ MMCME* || SITE_TYPE =~ PLLE*}]
    foreach site $clockSites {
        set cr [get_property CLOCK_REGION $site]
        set slr -1
        if {[dict exists $crSlr $cr]} {set slr [dict get $crSlr $cr]}
        row $output [list $site [get_property SITE_TYPE $site] $cr $slr \
            [get_tiles -of_objects $site] [get_property PROHIBIT $site]]
    }
    close $output
    row $queries [list clock_sites ok [llength $clockSites]]

    # Save the complete native report. Some Vivado/device combinations expose
    # routing-track Availability before placement; others omit those sections.
    # The report's Used fields always describe the open design, not a capacity.
    set reportStatus ok
    if {[catch {report_clock_utilization -file $out/preplacement_clock_utilization.rpt} message]} {
        set reportStatus unavailable
        row $queries [list report_clock_utilization unavailable $message]
    } elseif {![file exists $out/preplacement_clock_utilization.rpt] || [file size $out/preplacement_clock_utilization.rpt] == 0} {
        set reportStatus unavailable
        row $queries [list report_clock_utilization unavailable "No nonempty report was produced"]
    } else {
        row $queries [list report_clock_utilization ok preplacement_clock_utilization.rpt]
    }
    close $queries

    set meta [open $out/clock_metadata.tsv w]
    row $meta [list schema_version 1]
    row $meta [list part $partName]
    row $meta [list vivado [version -short]]
    row $meta [list scope nominal-device-evidence]
    row $meta [list availability_source [expr {$partMode ? {empty-device-design} : {input-checkpoint}}]]
    row $meta [list placement_run_by_exporter 0]
    row $meta [list report_design_state [expr {$partMode ? {unplaced-tiny-ooc-probe} : {input-checkpoint-state-not-modified}}]]
    foreach property {FAMILY ARCHITECTURE DEVICE} {
        set key [string tolower $property]
        if {[dict exists $partValues $property] && [dict get $partValues $property] ne ""} {
            set value [dict get $partValues $property]
            if {$property eq "FAMILY"} {set value [string tolower $value]}
            row $meta [list $key $value]
            row $meta [list ${key}_status ok]
        } else {
            row $meta [list $key unavailable]
            row $meta [list ${key}_status unavailable]
        }
    }
    row $meta [list clock_region_count [dict size $crSlr]]
    row $meta [list clock_site_count [llength $clockSites]]
    row $meta [list clock_site_scope "BUFG*,BUFCE*,BUFH*,BUFR*,BUFIO*,BUFMR*,MMCME*,PLLE* site types; inventory is not track capacity"]
    row $meta [list clock_utilization_report_status $reportStatus]
    row $meta [list capacity_resolution "Only report Availability fields or separately validated architecture rules; never site count or Used fields"]
    row $meta [list design_occupancy "No design-specific reservations subtracted from nominal capacities"]
    close $meta
    puts "AMF_CLOCK_RESOURCE_EXPORT_OK=[dict size $crSlr],[llength $clockSites],report:$reportStatus"
}
