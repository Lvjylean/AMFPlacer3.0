namespace eval amf_dfx {
    proc timed {label script} {
        global dfx
        set start [clock milliseconds]
        set code [catch {uplevel 1 $script} value options]
        set seconds [expr {([clock milliseconds] - $start)/1000.0}]
        set f [open $dfx(output)/stage_times.tsv a]
        puts $f "$label\t$seconds\t$code"; close $f
        puts "AMF_DFX_STAGE $label seconds=$seconds code=$code"
        if {$code} {return -options $options $value}
        return $value
    }

    proc active {name} {
        global dfx
        return [expr {$name eq $dfx(active_partition) || [string first "$dfx(active_partition)/" $name] == 0}]
    }
    proc primitive_cells {} { return [get_cells -hierarchical -filter {IS_PRIMITIVE == 1}] }
    proc constant {cell} { return [expr {[get_property REF_NAME $cell] in {GND VCC}}] }
    proc static_snapshot {} {
        set data [dict create]
        foreach cell [primitive_cells] {
            if {[active $cell] || [constant $cell]} { continue }
            set loc [get_property LOC $cell]
            set bel [get_property BEL $cell]
            if {$loc eq "" || $bel eq ""} { error "Unplaced static primitive: $cell" }
            # read_checkpoint may invalidate every Vivado handle. Snapshot plain names, not handles.
            dict set data [get_property NAME $cell] [list [get_property REF_NAME $cell] $loc $bel]
        }
        return $data
    }
    proc save_dict {path data} {
        set f [open $path w]; puts $f $data; close $f
    }
    proc read_dict {path} {
        set f [open $path r]; set result [string trim [read $f]]; close $f
        dict size $result
        return $result
    }
    proc assert_same {before after label} {
        if {[lsort [dict keys $before]] ne [lsort [dict keys $after]]} {
            set removed {}; set added {}
            dict for {k v} $before {if {![dict exists $after $k]} {lappend removed $k}}
            dict for {k v} $after {if {![dict exists $before $k]} {lappend added $k}}
            error "$label membership changed: removed=$removed added=$added"
        }
        dict for {name value} $before {
            if {$value ne [dict get $after $name]} {error "$label changed at $name"}
        }
    }
    proc assert_context {} {
        global dfx
        if {[get_property PART [current_design]] ne $dfx(part)} {error "DFX part mismatch"}
        set rp [get_cells -quiet $dfx(active_partition)]
        if {[llength $rp] != 1 || ![get_property HD.RECONFIGURABLE $rp]} {error "RP is not HD.RECONFIGURABLE"}
        if {[llength [get_pblocks -quiet $dfx(pblock)]] != 1} {error "Missing Pblock"}
        if {[lsearch -exact [get_pblocks -of_objects $rp] $dfx(pblock)] < 0} {error "Active RP is not assigned to the selected Pblock"}
        if {[get_property IS_SOFT [get_pblocks $dfx(pblock)]]} {error "DFX requires a hard placement Pblock"}
        set clock [get_clocks -quiet $dfx(clock_name)]
        if {[llength $clock] != 1 || abs([get_property PERIOD $clock] - $dfx(clock_period_ns)) > 0.000001} {
            error "Explicit clock period does not agree with DCP constraints"
        }
    }
    proc boundary_snapshot {} {
        global dfx
        set result [dict create]
        foreach pin [get_pins -of_objects [get_cells $dfx(active_partition)]] {
            # Empty PPLOC is legitimate (dedicated clocks or reduced partition
            # pins). Preserve every assigned location; never select only lindex 0.
            dict set result [get_property REF_PIN_NAME $pin] [list [get_property DIRECTION $pin] \
                [get_property HD.ASSIGNED_PPLOCS $pin]]
        }
        return $result
    }
    proc boundary_signature {} {
        set result [dict create]
        dict for {name details} [boundary_snapshot] {dict set result $name [lindex $details 0]}
        return $result
    }
    proc audit {out stage} {
        global dfx
        assert_context
        assert_same [read_dict $out/static_snapshot.tcldata] [static_snapshot] "Static placement"
        report_timing_summary -delay_type min_max -report_unconstrained -file $out/${stage}_timing.rpt
        report_timing -through [get_pins -of_objects [get_cells $dfx(active_partition)]] \
            -max_paths 100 -path_type full_clock_expanded -file $out/${stage}_boundary_timing.rpt
        report_drc -file $out/${stage}_drc.rpt
        set f [open $out/${stage}_drc_counts.tsv w]
        puts $f "errors\t[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]"
        puts $f "critical_warnings\t[llength [get_drc_violations -quiet -filter {SEVERITY == "Critical Warning"}]]"
        close $f
    }
}
