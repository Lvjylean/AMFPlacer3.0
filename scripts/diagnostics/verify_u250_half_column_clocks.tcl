# Probe simultaneous global clocks in one U250 SLICE half-column.
# Usage: vivado -mode batch -source this.tcl -tclargs PART OUT N SLICE_X Y0
# Example: ... -tclargs xcu250-figd2104-2L-e /new/evidence/n13 13 117 360
# OOC removes board I/O from this resource experiment. BUFGCEs are INSIDE
# the OOC module so that their output clocks must actually be routed.
# No CLOCK_DEDICATED_ROUTE exception, clock-routing relaxation, or DCP export.

if {[llength $argv] != 5} {
    error "Expected PART OUT N SLICE_X Y0"
}
lassign $argv part out n sliceX y0
foreach variable {n sliceX y0} {
    if {![string is integer -strict [set $variable]]} {
        error "$variable must be an integer"
    }
}
if {$n < 1 || $n > 30 || $sliceX < 0 || $y0 < 0 || $y0 % 30 != 0} {
    error "Require 1 <= N <= 30, SLICE_X >= 0, and Y0 >= 0 divisible by 30"
}
if {![regexp -nocase {^xcu250-} $part]} {error "This diagnostic is scoped to XCU250"}
set out [file normalize $out]
if {[file exists $out]} {error "Refusing to overwrite $out"}
file mkdir $out
set started [clock seconds]
set stage initialize
set stageLog [open [file join $out stages.tsv] w]
puts $stageLog "stage\tstate\tepoch_seconds\tdetail"
proc status {name state {detail ""}} {
    global stageLog
    puts $stageLog [join [list $name $state [clock seconds] \
        [string map [list "\t" " " "\n" " " "\r" " "] $detail]] "\t"]
    flush $stageLog
}
proc run_stage {name body} {
    global stage
    set stage $name
    status $name running
    uplevel 1 $body
    status $name completed
}
proc property_or_na {object property} {
    if {[lsearch -exact [list_property $object] $property] >= 0} {
        return [get_property $property $object]
    }
    return unavailable
}
proc snapshot {prefix} {
    global out n
    set cellsOut [open [file join $out ${prefix}_cells.tsv] w]
    puts $cellsOut "cell\tref_name\tloc\tbel\tis_loc_fixed\tclock_region"
    foreach cell [get_cells -quiet -hierarchical -filter {IS_PRIMITIVE}] {
        puts $cellsOut [join [list $cell [get_property REF_NAME $cell] \
            [property_or_na $cell LOC] [property_or_na $cell BEL] \
            [property_or_na $cell IS_LOC_FIXED] [property_or_na $cell CLOCK_REGION]] "\t"]
    }
    close $cellsOut
    set netsOut [open [file join $out ${prefix}_clock_nets.tsv] w]
    puts $netsOut "index\tnet\troute_status\tclock_root\tload_pins\tnode_count\tleaf_count\tleaf_nodes"
    set nodesOut [open [file join $out ${prefix}_clock_nodes.tsv] w]
    puts $nodesOut "index\tnet\tnode\tintent_code_name"
    for {set i 0} {$i < $n} {incr i} {
        set driver [get_pins -quiet clkbuf_${i}/O]
        if {[llength $driver] == 0} {continue}
        foreach net [get_nets -quiet -of_objects $driver] {
            set nodes [get_nodes -quiet -of_objects $net]
            set leaves {}
            foreach node $nodes {
                set intent [property_or_na $node INTENT_CODE_NAME]
                puts $nodesOut [join [list $i $net $node $intent] "\t"]
                if {$intent eq "NODE_GLOBAL_LEAF"} {lappend leaves $node}
            }
            set leaves [lsort -unique $leaves]
            set loads [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == IN}]
            puts $netsOut [join [list $i $net [property_or_na $net ROUTE_STATUS] \
                [property_or_na $net CLOCK_ROOT] [join $loads ,] [llength $nodes] \
                [llength $leaves] [join $leaves ,]] "\t"]
        }
    }
    close $netsOut
    close $nodesOut
}
proc assert_preserved {} {
    global n sliceX y0
    if {[llength [get_cells -hierarchical -filter {REF_NAME == BUFGCE}]] != $n ||
        [llength [get_cells -hierarchical -filter {REF_NAME == FDRE}]] != $n} {
        error "BUFGCE/FDRE count changed; this run cannot establish a clock capacity"
    }
    set distinct {}
    for {set i 0} {$i < $n} {incr i} {
        set reg [get_cells ff_$i]
        set expected SLICE_X${sliceX}Y[expr {$y0 + $i}]
        if {[get_property LOC $reg] ne $expected} {error "Unexpected FF location: $reg"}
        set net [get_nets -of_objects [get_pins clkbuf_${i}/O]]
        set ffNet [get_nets -of_objects [get_pins ff_${i}/C]]
        if {[llength $net] != 1 || "$net" ne "$ffNet"} {
            error "Clock $i no longer directly connects its own BUFGCE to FDRE"
        }
        lappend distinct [get_property NAME $net]
    }
    if {[llength [lsort -unique $distinct]] != $n} {error "Clock nets were merged"}
}

if {[catch {
    set_param general.maxThreads 4
    set meta [open [file join $out metadata.tsv] w]
    foreach {key value} [list part $part vivado [version -short] clock_count $n \
        slice_x $sliceX y0 $y0 y_last [expr {$y0 + 29}] clock_period_ns 10 \
        design_mode out_of_context source_clock_ports independent \
        clock_buffers inside_ooc clock_routing_relaxation none \
        interpretation resource_probe_not_benchmark_qor] {puts $meta "$key\t$value"}
    close $meta
    run_stage generate_rtl {
        set rtl [open [file join $out half_column_probe.v] w]
        set ports {}
        for {set i 0} {$i < $n} {incr i} {
            lappend ports "input wire clk_$i" "input wire d_$i" "output wire q_$i"
        }
        puts $rtl "module half_column_probe([join $ports {, }]);"
        for {set i 0} {$i < $n} {incr i} {
            puts $rtl "(* KEEP = \"TRUE\", DONT_TOUCH = \"TRUE\" *) wire gclk_$i;"
            puts $rtl "(* DONT_TOUCH = \"TRUE\" *) BUFGCE clkbuf_$i (.I(clk_$i), .CE(1'b1), .O(gclk_$i));"
            puts $rtl "(* DONT_TOUCH = \"TRUE\" *) FDRE ff_$i (.C(gclk_$i), .CE(1'b1), .R(1'b0), .D(d_$i), .Q(q_$i));"
        }
        puts $rtl "endmodule"
        close $rtl
    }
    run_stage synthesize {
        create_project -in_memory -part $part
        read_verilog [file join $out half_column_probe.v]
        synth_design -top half_column_probe -mode out_of_context -flatten_hierarchy none -bufg 32
    }
    run_stage constrain {
        set crNames {}
        set geometry [open [file join $out half_column_sites.tsv] w]
        puts $geometry "site\tsite_type\tclock_region\ttile"
        for {set row 0} {$row < 30} {incr row} {
            set site [get_sites -quiet SLICE_X${sliceX}Y[expr {$y0 + $row}]]
            if {[llength $site] != 1} {error "Missing or ambiguous half-column site at row $row"}
            set cr [get_property CLOCK_REGION $site]
            if {$cr eq ""} {error "Site without clock region: $site"}
            lappend crNames $cr
            puts $geometry [join [list $site [get_property SITE_TYPE $site] $cr \
                [get_tiles -of_objects $site]] "\t"]
        }
        close $geometry
        if {[llength [lsort -unique $crNames]] != 1} {error "Requested 30 rows cross clock regions"}
        set groups {}
        for {set i 0} {$i < $n} {incr i} {
            create_clock -name clk_$i -period 10 [get_ports clk_$i]
            lappend groups -group [get_clocks clk_$i]
            set_property LOC SLICE_X${sliceX}Y[expr {$y0 + $i}] [get_cells ff_$i]
            set_property DONT_TOUCH TRUE [get_cells [list clkbuf_$i ff_$i]]
            set_property KEEP TRUE [get_nets -of_objects [get_pins clkbuf_${i}/O]]
            set_property DONT_TOUCH TRUE [get_nets -of_objects [get_pins clkbuf_${i}/O]]
        }
        if {$n > 1} {set_clock_groups -asynchronous {*}$groups}
        assert_preserved
        write_xdc -force [file join $out probe_constraints.xdc]
    }
    run_stage optimize {opt_design; assert_preserved}
    run_stage place {place_design; assert_preserved; snapshot placed}
    run_stage route {route_design; assert_preserved; snapshot routed}
    run_stage reports {
        report_route_status -file [file join $out route_status.rpt]
        report_clock_utilization -file [file join $out clock_utilization.rpt]
        report_drc -file [file join $out drc.rpt]
        report_utilization -file [file join $out utilization.rpt]
    }
    run_stage verify_clock_routes {
        set allLeaves {}
        for {set i 0} {$i < $n} {incr i} {
            set net [get_nets -of_objects [get_pins clkbuf_${i}/O]]
            if {[get_property ROUTE_STATUS $net] ne "ROUTED"} {
                error "Clock $i is not fully routed: [get_property ROUTE_STATUS $net]"
            }
            set leaves {}
            foreach node [get_nodes -of_objects $net] {
                if {[property_or_na $node INTENT_CODE_NAME] eq "NODE_GLOBAL_LEAF"} {lappend leaves $node}
            }
            set leaves [lsort -unique $leaves]
            if {[llength $leaves] != 1} {error "Clock $i uses [llength $leaves] leaf nodes; inspect the recorded topology"}
            lappend allLeaves [lindex $leaves 0]
        }
        if {[llength [lsort -unique $allLeaves]] != $n} {error "Clocks share leaf resources"}
    }
    set result [open [file join $out result.tsv] w]
    puts $result "state\tcompleted"
    puts $result "distinct_routed_clock_nets\t$n"
    puts $result "distinct_used_leaf_nodes\t[llength [lsort -unique $allLeaves]]"
    puts $result "elapsed_seconds\t[expr {[clock seconds] - $started}]"
    puts $result "conclusion\tdemonstrates_at_least_${n}_simultaneous_clocks_in_requested_half_column"
    close $result
    close $stageLog
} message options]} {
    status $stage failed $message
    # Capture partial placement/routing even when the implementation command fails.
    if {[catch {snapshot failed} snapshotError]} {status failure_snapshot unavailable $snapshotError}
    if {[catch {report_route_status -file [file join $out failed_route_status.rpt]} reportError]} {
        status failure_route_report unavailable $reportError
    }
    set errorOut [open [file join $out error.txt] w]
    puts $errorOut "stage=$stage"
    puts $errorOut $message
    if {[dict exists $options -errorinfo]} {puts $errorOut [dict get $options -errorinfo]}
    close $errorOut
    set result [open [file join $out result.tsv] w]
    puts $result "state\tfailed"
    puts $result "failed_stage\t$stage"
    puts $result "elapsed_seconds\t[expr {[clock seconds] - $started}]"
    puts $result "conclusion\tfailure_alone_does_not_establish_a_hardware_capacity_upper_bound"
    close $result
    close $stageLog
    puts stderr $message
    exit 1
}
puts "AMF_HALF_COLUMN_PROBE_OK=$n"
exit 0
