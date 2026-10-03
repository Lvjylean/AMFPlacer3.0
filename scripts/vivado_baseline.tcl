# Native placement/routing from the same checkpoint as a recorded AMF backend.
if {$argc != 12} {error "Expected input.dcp reports-directory part clock-count period-ns opt-design release-io preserve-input core-clock vivado-version physical-audit resume"}
lassign $argv input out expected_part expected_clocks expected_period run_opt release_io preserve_input core_clock expected_version physical_audit resume
set inputs [file dirname [info script]]
set_param general.maxThreads 4
set timeline [open [file join $out stages.tsv] w]
puts $timeline "stage\tseconds\ttcl_status"
proc timed {name body} {
    global timeline out
    set f [open [file join $out current_stage.txt] w];puts $f $name;close $f
    puts "VIVADO_NATIVE_STAGE_START $name";flush stdout
    set begin [clock milliseconds]
    set code [catch {uplevel 1 $body} value opts]
    set elapsed [expr {([clock milliseconds]-$begin)/1000.0}]
    puts $timeline "$name\t$elapsed\t$code";flush $timeline
    puts "VIVADO_NATIVE_STAGE_FINISH $name $elapsed status=$code";flush stdout
    if {$code} {return -options $opts $value}
    return $value
}
proc ports_snapshot {} {
    set rows {}
    foreach p [get_ports] {
        lappend rows [list $p [get_property PACKAGE_PIN $p] [get_property IOSTANDARD $p]]
    }
    return [lsort $rows]
}
proc clocks_snapshot {} {
    set rows {}
    foreach c [get_clocks *] {
        lappend rows [list $c [get_property PERIOD $c] [get_property WAVEFORM $c]]
    }
    return [lsort $rows]
}
proc standards_snapshot {} {
    set rows {}
    foreach p [get_ports] {lappend rows [list $p [get_property IOSTANDARD $p]]}
    return [lsort $rows]
}
proc fixed_snapshot {} {
    set rows {}
    foreach c [get_cells -hier -quiet -filter {IS_LOC_FIXED || IS_BEL_FIXED}] {
        set loc "";set bel ""
        if {[get_property IS_LOC_FIXED $c]} {set loc [get_property LOC $c]}
        if {[get_property IS_BEL_FIXED $c]} {set bel [get_property BEL $c]}
        lappend rows [list $c $loc $bel]
    }
    return [lsort $rows]
}
proc read_tsv {path} {
    set f [open $path];gets $f header;set rows {}
    while {[gets $f line] >= 0} {
        set line [string trimright $line \r]
        if {$line ne ""} {lappend rows [split $line "\t"]}
    }
    close $f;return $rows
}
proc bel_leaf {bel} {return [lindex [split [string map {. /} $bel] /] end]}
proc audit_fixed_locations {stage} {
    global original_fixed inputs out
    set allowed [dict create]
    foreach row [read_tsv [file join $inputs reference_fixed_relocations.tsv]] {
        dict set allowed [lindex $row 0] [lrange $row 1 end]
    }
    set accepted 0;set unexpected {}
    set f [open [file join $out ${stage}_fixed_locations.tsv] w]
    puts $f "cell\trequested_loc\trequested_bel\tactual_loc\tactual_bel\tclassification"
    foreach row $original_fixed {
        lassign $row name loc bel
        set c [get_cells -quiet $name]
        set actual_loc "";set actual_bel "";set classification exact
        if {[llength $c] == 1} {
            set actual_loc [get_property LOC $c];set actual_bel [get_property BEL $c]
        }
        if {[llength $c] != 1 || ($loc ne "" && $actual_loc ne $loc) || ($bel ne "" && [bel_leaf $actual_bel] ne [bel_leaf $bel])} {
            set classification unexpected
            if {[llength $c] == 1 && [string match BUFG* [get_property REF_NAME $c]] && [dict exists $allowed $name]} {
                set transition [list $loc [bel_leaf $bel] $actual_loc [bel_leaf $actual_bel]]
                if {$transition eq [dict get $allowed $name]} {
                    set classification matches_reference_vivado_repair;incr accepted
                }
            }
            if {$classification eq "unexpected"} {lappend unexpected $name}
        }
        puts $f [join [list $name $loc $bel $actual_loc $actual_bel $classification] "\t"]
    }
    close $f
    puts "VIVADO_NATIVE_FIXED_AUDIT stage=$stage matched_reference_repairs=$accepted unexpected=[llength $unexpected]"
    if {[llength $unexpected]} {error "Unexpected input fixed location changes: $unexpected"}
    return $accepted
}
proc congestion_report {name} {
    global out
    if {[catch {report_design_analysis -congestion -min_congestion_level 3 -file [file join $out $name]} message]} {
        set f [open [file join $out congestion_errors.log] a];puts $f "$name: $message";close $f
        puts "VIVADO_NATIVE_CONGESTION_REPORT_UNAVAILABLE $name: $message"
    }
}
proc write_ports {name} {
    global out
    set f [open [file join $out $name] w]
    puts $f "port\tpackage_pin\tiostandard"
    foreach row [ports_snapshot] {puts $f [join $row "\t"]}
    close $f
}
proc inventory {stage} {
    global out
    set cells [get_cells -hier -filter {IS_PRIMITIVE}]
    set names [get_property NAME $cells]
    array set known {}
    foreach n $names {set known($n) 1}
    set counts [dict create]
    set total 0
    foreach n $names type [get_property REF_NAME $cells] {
        set parent $n;set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            if {[info exists known($parent)]} {set internal 1;break}
        }
        if {!$internal} {dict incr counts $type;incr total}
    }
    set f [open [file join $out ${stage}_primitive_counts.tsv] w]
    puts $f "primitive\tcount"
    foreach type [lsort [dict keys $counts]] {puts $f "$type\t[dict get $counts $type]"}
    puts $f "TOTAL\t$total";close $f
    puts "VIVADO_NATIVE_INVENTORY $stage $total"
    return $counts
}
if {[catch {
    timed open {open_checkpoint $input}
    if {[version -short] ne $expected_version} {error "Vivado version differs from reference"}
    if {[get_property PART [current_design]] ne $expected_part} {error "Unexpected part"}
    if {$preserve_input} {
        if {$release_io} {error "Cannot release board I/O in preserve-input mode"}
        set core [get_clocks -quiet $core_clock]
        if {[llength $core] != 1 || abs([get_property PERIOD $core]-$expected_period)>0.000001} {
            error "Core clock does not match the requested period"
        }
    } else {
        if {[llength [get_clocks *]] != $expected_clocks} {error "Unexpected clock count"}
        foreach c [get_clocks *] {
            if {[get_property PERIOD $c] != $expected_period} {error "Unexpected clock period on $c"}
        }
    }
    if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Black boxes present"}
    if {!$preserve_input && [llength [get_pblocks -quiet *]]} {error "Unexpected input floorplan constraints"}
    set original_ports [ports_snapshot]
    set original_standards [standards_snapshot]
    set original_clocks [clocks_snapshot]
    set original_fixed [fixed_snapshot]
    if {$resume} {
        set original_fixed [read_tsv [file join $inputs original_input_fixed_cells.tsv]]
        set saved_ports [lsort [read_tsv [file join $inputs original_input_ports.tsv]]]
        if {$original_ports ne $saved_ports} {error "Recovery I/O differs from the original input"}
        set saved_clocks [read_tsv [file join $inputs original_clocks.tsv]]
        if {[llength $saved_clocks] != [llength $original_clocks]} {error "Recovery clock count differs"}
        set lookup [dict create]
        foreach row $original_clocks {dict set lookup [lindex $row 0] [lrange $row 1 end]}
        foreach row $saved_clocks {
            lassign $row name period waveform source
            if {![dict exists $lookup $name]} {error "Recovery clock missing: $name"}
            lassign [dict get $lookup $name] actual_period actual_wave
            if {abs($period-$actual_period)>0.000001} {error "Recovery clock period differs: $name"}
            if {[llength $waveform] != [llength $actual_wave]} {error "Recovery waveform differs: $name"}
            foreach a $waveform b $actual_wave {if {abs($a-$b)>0.000001} {error "Recovery waveform differs: $name"}}
        }
        audit_fixed_locations recovery_input
    }
    timed input_audit {
        set original_inventory [inventory input]
        write_ports input_ports.tsv
        report_clocks -file [file join $out clocks.rpt]
        check_timing -verbose -file [file join $out input_check_timing.rpt]
        write_xdc -type timing [file join $out input_timing.xdc]
        write_xdc -type physical [file join $out input_physical.xdc]
        set f [open [file join $out input_fixed_cells.tsv] w]
        puts $f "cell\tloc\tbel"
        foreach row $original_fixed {puts $f [join $row "\t"]};close $f
        set f [open [file join $out tools.json] w]
        puts $f [format {{"vivado_version":"%s","part":"%s","max_threads":4}} [version -short] [get_property PART [current_design]]];close $f
    }
    if {$preserve_input} {
        set f [open [file join $out release_placement.json] w]
        puts $f [format {{"preserve_input_constraints":true,"released_loc_fixed_cells":0,"released_bel_fixed_cells":0,"input_fixed_cells":%d,"io_package_pins_preserved":true,"io_standards_preserved":true,"clock_constraints_preserved":true,"amf_placement_imported":false}} [llength $original_fixed]];close $f
    } else {timed release_placement {
        set fixed [get_cells -hier -quiet -filter {IS_LOC_FIXED}]
        if {[llength $fixed]} {set_property IS_LOC_FIXED false $fixed}
        set fixed_bel [get_cells -hier -quiet -filter {IS_BEL_FIXED}]
        if {[llength $fixed_bel]} {set_property IS_BEL_FIXED false $fixed_bel}
        place_design -unplace
        # Clear explicit BUFG location constraints even if unplace retained them.
        set buffers [get_cells -hier -quiet -filter {REF_NAME == BUFGCE || REF_NAME == BUFGCTRL || REF_NAME == BUFGCE_DIV}]
        if {[llength $buffers]} {
            reset_property LOC $buffers
            reset_property BEL $buffers
            unplace_cell $buffers
        }
        set remaining [get_cells -hier -quiet -filter {IS_LOC_FIXED || IS_BEL_FIXED}]
        if {[llength $remaining]} {error "Placement remains fixed: $remaining"}
        # place_design -unplace clears auto-assigned PACKAGE_PIN values too.
        # Reapply the exact saved I/O boundary while leaving fabric/BUFG free.
        set restored_pins 0
        if {$release_io} {
            set assigned_ports [get_ports -quiet -filter {PACKAGE_PIN != ""}]
            if {[llength $assigned_ports]} {reset_property PACKAGE_PIN $assigned_ports}
        }
        foreach row $original_ports {
            lassign $row port pin standard
            if {!$release_io && $pin ne ""} {
                set_property PACKAGE_PIN $pin [get_ports $port]
                incr restored_pins
            }
            if {$standard ne ""} {set_property IOSTANDARD $standard [get_ports $port]}
        }
        if {!$release_io && [ports_snapshot] ne $original_ports} {error "I/O constraints changed"}
        if {[standards_snapshot] ne $original_standards} {error "I/O standards changed"}
        if {[clocks_snapshot] ne $original_clocks} {error "Clock constraints changed"}
        set f [open [file join $out release_placement.json] w]
        puts $f [format {{"released_loc_fixed_cells":%d,"released_bel_fixed_cells":%d,"fixed_cells_after_unplace_before_io_restore":0,"restored_package_pins":%d,"io_package_pins_preserved":%s,"io_standards_preserved":true,"clock_constraints_preserved":true,"amf_placement_imported":false}} [llength $fixed] [llength $fixed_bel] $restored_pins [expr {$release_io ? "false" : "true"}]];close $f
        write_xdc -type physical [file join $out released_physical.xdc]
    }}
    if {!$resume} {
      if {$run_opt} {timed opt {opt_design}}
      timed place {place_design}
      timed placed_reports {
        inventory placed
        report_clock_utilization -file [file join $out placed_clock_utilization.rpt]
        report_utilization -file [file join $out placed_utilization.rpt]
        report_timing_summary -delay_type min_max -report_unconstrained -file [file join $out placed_timing_summary.rpt]
        congestion_report congestion_post_place.rpt
    }
      timed placed_checkpoint {write_checkpoint [file join $out vivado_placed.dcp]}
    }
    timed route {route_design}
    # Preserve the expensive completed route even if a later report/audit fails.
    timed routed_checkpoint {write_checkpoint [file join $out vivado_routed.dcp]}
    timed reports {
        inventory routed
        report_route_status -file [file join $out route_status.rpt]
        report_drc -file [file join $out drc.rpt]
        set f [open [file join $out drc_counts.json] w]
        puts $f [format {{"errors":%d,"critical_warnings":%d}} [llength [get_drc_violations -quiet -filter {SEVERITY == Error}]] [llength [get_drc_violations -quiet -filter {SEVERITY == "Critical Warning"}]]];close $f
        report_timing_summary -delay_type min_max -report_unconstrained -file [file join $out timing_summary.rpt]
        check_timing -verbose -file [file join $out check_timing.rpt]
        report_utilization -file [file join $out utilization.rpt]
        report_clock_utilization -file [file join $out clock_utilization.rpt]
        congestion_report congestion_post_route.rpt
        report_timing -delay_type max -max_paths 10 -nworst 1 -path_type full_clock_expanded -file [file join $out worst_setup_paths.rpt]
        if {!$release_io && [ports_snapshot] ne $original_ports} {error "I/O constraints changed during implementation"}
        if {[standards_snapshot] ne $original_standards} {error "I/O standards changed during implementation"}
        write_ports final_ports.tsv
        if {[clocks_snapshot] ne $original_clocks} {error "Clock constraints changed during implementation"}
        if {$preserve_input} {
            set matched_repairs [audit_fixed_locations routed]
        } else {
            set matched_repairs 0
        }
        set f [open [file join $out constraint_audit.json] w]
        puts $f [format {{"clock_snapshot_unchanged":true,"io_standards_unchanged":true,"input_fixed_locations_verified":%s,"core_period_ns":%s,"accepted_reference_clock_relocations":%d,"strict_input_fixed_locations":%s}} [expr {$preserve_input ? "true" : "false"}] $expected_period $matched_repairs [expr {$matched_repairs==0 ? "true" : "false"}]];close $f
        write_xdc -type timing [file join $out final_timing.xdc]
    }
    if {$physical_audit} {
        source [file join [file dirname [info script]] export_boundary_timing_samples.tcl]
        timed boundary_samples {export_boundary_timing_samples [file join $out physical]}
    }
    puts "VIVADO_NATIVE_FLOW_FINISHED"
} message options]} {
    puts stderr "VIVADO_NATIVE_FLOW_FAILED: $message"
    puts stderr [dict get $options -errorinfo]
    set f [open [file join $out failure.txt] w];puts $f $message;puts $f [dict get $options -errorinfo];close $f
    close $timeline
    exit 2
}
close $timeline
exit 0
