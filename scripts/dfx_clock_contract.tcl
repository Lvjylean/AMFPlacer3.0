# Explicit single-primary-clock contract for the validated joint GETRF flow.
# Other jobs retain their existing behavior unless this contract is enabled.
namespace eval dfx_clock_contract {
    proc enabled {} {
        global dfx
        return [expr {[info exists dfx(clock_contract_enabled)] && $dfx(clock_contract_enabled)}]
    }
    proc signature {} {
        set result {}
        foreach clk [get_clocks -quiet *] {
            dict set result [get_property NAME $clk] [list \
                [lsort [get_property SOURCE_PINS $clk]] \
                [get_property PERIOD $clk] [get_property WAVEFORM $clk] \
                [get_property IS_GENERATED $clk]]
        }
        return $result
    }
    proc assert_top {label {check_rps 0}} {
        global dfx
        if {![enabled]} {return}
        current_instance
        set clocks [get_clocks -quiet *]
        set clk [get_clocks -quiet $dfx(clock_name)]
        if {[llength $clocks]!=1 || [llength $clk]!=1} {error "Clock inventory changed at $label"}
        if {[lsort [get_property SOURCE_PINS $clk]] ne [list $dfx(clock_root_pin)]} {
            error "Clock source changed at $label: [get_property SOURCE_PINS $clk]"
        }
        if {[get_property IS_GENERATED $clk] || abs([get_property PERIOD $clk]-$dfx(clock_period_ns))>1e-6} {
            error "Primary clock period/type changed at $label"
        }
        if {$check_rps} {
            foreach row $dfx(rps) {
                lassign $row id rp pb
                set pin [get_pins -quiet [list $rp/$dfx(clock_rm_port)]]
                if {[llength $pin]!=1} {error "Missing RP clock port at $label/$id"}
                set seen [get_clocks -quiet -of_objects $pin]
                if {[llength $seen]!=1 || [get_property NAME $seen] ne $dfx(clock_name)} {
                    error "RP does not inherit the top clock at $label/$id"
                }
            }
        }
        set f [open $dfx(output)/clock_contract.tsv a]
        puts $f [join [list $label $dfx(clock_name) [get_property SOURCE_PINS $clk] [get_property PERIOD $clk] $check_rps] \t]
        close $f
        puts "AMF_CLOCK_SOURCE_EXACT stage=$label source=$dfx(clock_root_pin)"
    }
    proc assert_unchanged {expected label} {
        if {![enabled]} {return}
        assert_top $label
        amf_dfx::assert_same $expected [signature] "Clock ownership $label"
    }
    proc assert_coverage {report label} {
        global dfx
        if {![enabled]} {return}
        set f [open $report r];set text [read $f];close $f
        set f [open $dfx(output)/clock_coverage.tsv a]
        set failed {}
        foreach key {no_clock unconstrained_internal_endpoints} {
            set pattern [format {checking %s \(([0-9]+)\)} $key]
            if {![regexp $pattern $text match count]} {close $f;error "Missing timing coverage field: $key"}
            set limit $dfx(clock_max_$key)
            puts $f "$label\t$key\t$count\t$limit"
            if {$count>$limit} {lappend failed "$key=$count exceeds baseline=$limit"}
        }
        close $f
        if {[llength $failed]} {error "Incomplete timing coverage at $label: [join $failed {; }]"}
        puts "AMF_CLOCK_COVERAGE_EXACT stage=$label"
    }
}
