# Capture IP ownership/clock constraints before AMF import and enforce them later.
namespace eval amf_contract {
    variable baseline {}
    proc signature {} {
        set rows {}
        foreach cell [get_cells -hier -quiet -filter {DONT_TOUCH == TRUE || ASYNC_REG == TRUE}] {
            lappend rows [list cell $cell [get_property REF_NAME $cell] [get_property DONT_TOUCH $cell] [get_property ASYNC_REG $cell]]
        }
        foreach net [get_nets -hier -quiet -filter {DONT_TOUCH == TRUE}] {
            lappend rows [list net $net [lsort [get_pins -quiet -of_objects $net]] [lsort [get_ports -quiet -of_objects $net]]]
        }
        foreach clk [get_clocks] {
            lappend rows [list clock $clk [get_property PERIOD $clk] [get_property WAVEFORM $clk] [get_property SOURCE_PINS $clk]]
        }
        foreach cls {net pin} {
            if {$cls eq "net"} {set objects [get_nets -hier -quiet -filter {CLOCK_DEDICATED_ROUTE != "" && CLOCK_DEDICATED_ROUTE != TRUE}]} else {
                set objects [get_pins -hier -quiet -filter {CLOCK_DEDICATED_ROUTE != "" && CLOCK_DEDICATED_ROUTE != TRUE}]
            }
            foreach obj $objects {lappend rows [list clock_route $cls $obj [get_property CLOCK_DEDICATED_ROUTE $obj]]}
        }
        foreach port [get_ports] {
            lappend rows [list port $port [get_property DIRECTION $port] [get_property PACKAGE_PIN $port] [get_property IOSTANDARD $port]]
        }
        return [lsort $rows]
    }
    proc capture {} {variable baseline;set baseline [signature]}
    proc check {placement output stage} {
        variable baseline
        if {[signature] ne $baseline} {error "IP protection/clock/port contract changed at $stage"}
        set f [open [file join $placement fixed_contract.tsv] r];gets $f header
        set count 0
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] name site bel mode
            set cell [get_cells -quiet $name]
            if {[llength $cell] != 1 || [get_property LOC $cell] ne $site || ![get_property IS_LOC_FIXED $cell]} {
                error "Fixed interface LOC/lock changed at $stage: $name"
            }
            if {$mode eq "bel" && ([lindex [split [get_property BEL $cell] .] end] ne $bel || ![get_property IS_BEL_FIXED $cell])} {
                error "Fixed interface BEL/lock changed at $stage: $name expected=$bel actual=[get_property BEL $cell] fixed=[get_property IS_BEL_FIXED $cell]"
            }
            incr count
        }
        close $f
        set report [open [file join $output ${stage}_interface_contract.tsv] w]
        puts $report "fixed_interfaces\t$count\nprotected_clock_port_records\t[llength $baseline]\ncontract_unchanged\t1"
        close $report
        set groups [file join $placement async_groups.txt]
        if {![file exists $groups]} {return}
        set f [open $groups r];gets $f header
        if {$header ne "AMF_ASYNC_GROUPS 1"} {error "Invalid synchronization group schema"}
        set locations [dict create]
        while {[gets $f line] >= 0} {
            lassign $line group name
            set site [get_property LOC [get_cells $name]]
            if {$site eq ""} {error "Unplaced ASYNC_REG member at $stage: $name"}
            dict lappend locations $group $site
        }
        close $f
        dict for {group sites} $locations {
            if {[llength [lsort -unique $sites]] != 1} {error "ASYNC_REG group split across SLICEs at $stage: $group"}
        }
    }
}
