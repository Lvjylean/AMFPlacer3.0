# The synthesized/optimized OOC design keeps its own timing clock. Only the
# completed placed-module handoff removes that primary, because static owns it.
namespace eval amf_ooc_clock {
    proc commands {path} {
        set f [open $path r];set text [read $f];close $f
        set result {};set buffer {}
        foreach line [split $text \n] {
            if {$buffer eq "" && ([string trim $line] eq "" || [string match {#*} [string trimleft $line]])} {continue}
            append buffer $line \n
            if {[info complete $buffer]} {
                # Vivado emits this exact root-scope trailer even for an empty timing set.
                if {[string trim $buffer] ne "current_instance -quiet"} {lappend result [string trim $buffer]}
                set buffer {}
            }
        }
        if {[string trim $buffer] ne ""} {error "Incomplete serialized timing command"}
        return $result
    }
    proc strip_checked_primary {} {
        global dfx
        set all [get_clocks -quiet *];set clk [get_clocks -quiet $dfx(clock_name)]
        if {[llength $all]!=1 || [llength $clk]!=1 || [get_property IS_GENERATED $clk]} {error "Handoff requires exactly one known OOC primary"}
        if {[get_property SOURCE_PINS $clk] ne $dfx(rm_clock_port) ||
            abs([get_property PERIOD $clk]-$dfx(clock_period_ns))>1e-6} {error "Unexpected OOC source or period"}
        set before $dfx(output)/ooc_timing_before_handoff.xdc
        write_xdc -type timing -constraints all -no_tool_comments $before
        set primary 0
        foreach cmd [commands $before] {
            if {[regexp {^create_clock\s} $cmd]} {incr primary;continue}
            # Source metadata carries no timing exception or physical setting.
            if {[regexp {^set_property\s+SRC_FILE_INFO\s} $cmd]} {continue}
            error "Additional OOC timing constraint requires explicit preservation: $cmd"
        }
        if {$primary!=1} {error "Serialized OOC timing contract is not one primary"}
        reset_timing
        if {[llength [get_clocks -quiet *]]!=0} {error "OOC primary survived reset_timing"}
        set after $dfx(output)/handoff_timing_after.xdc
        write_xdc -type timing -constraints all -no_tool_comments $after
        if {[llength [commands $after]]!=0} {error "Unexpected timing constraints remain in handoff"}
        set f [open $dfx(output)/clock_handoff.tsv w]
        puts $f "removed_primary\t$dfx(clock_name)\nsource\t$dfx(rm_clock_port)\nperiod_ns\t$dfx(clock_period_ns)\nother_timing_constraints\t0\ntop_clock_owner\tstatic_shell"
        close $f
        puts AMF_OOC_PRIMARY_REMOVED_FROM_HANDOFF
    }
}

