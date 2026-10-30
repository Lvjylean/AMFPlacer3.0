namespace eval full_carry_preroute {}
proc full_carry_preroute::initialize {name out} {
    # Run-scoped compatibility test for the one observed global-router failure.
    # Keep the existing partial FIXED_ROUTE and all selected SLL resources.
    set selected {}
    foreach record $::e2_sll::records {
        if {[dict get $record net] eq $name} {lappend selected $record}
    }
    if {[llength $selected]!=1} {error "CARRY preroute requires one original SLL audit record"}
    set record [lindex $selected 0]
    if {[dict get $record source_mode] ne "vivado_native_router_source"} {error "Unexpected CARRY source mode"}
    set net [get_nets -hierarchical -filter "NAME == \"$name\""]
    if {[llength $net]!=1} {error "CARRY preroute target is missing or ambiguous"}
    set drivers [get_pins -leaf -of_objects [get_nets -segments $net] -filter {DIRECTION == OUT}]
    if {[llength $drivers]!=1 || [get_property REF_NAME [get_cells -of_objects $drivers]] ne "CARRY8"} {error "CARRY preroute driver differs from confirmed case"}
    set pins [get_pins -leaf -of_objects [get_nets -segments $net]]
    set beforePins [lsort -unique [get_property NAME $pins]]
    set cells [lsort -unique [get_cells -of_objects $pins]]
    set beforePlacement [list [get_property LOC $cells] [get_property BEL $cells] [get_property LOCK_PINS $cells]]
    if {[get_property FIXED_ROUTE $net] eq ""} {error "CARRY preroute requires the original partial constraint"}
    set start [clock microseconds]
    route_design -nets $net
    set seconds [expr {([clock microseconds]-$start)/1e6}]
    set status [get_property ROUTE_STATUS $net]
    if {$status ne "ROUTED"} {error "CARRY preroute incomplete: $status"}
    set afterPins [lsort -unique [get_property NAME [get_pins -leaf -of_objects [get_nets -segments $net]]]]
    if {$beforePins ne $afterPins} {error "CARRY preroute changed connectivity"}
    if {$beforePlacement ne [list [get_property LOC $cells] [get_property BEL $cells] [get_property LOCK_PINS $cells]]} {error "CARRY preroute changed cell placement or pin locks"}
    set actual [get_property NAME [get_nodes -of_objects $net -filter {IS_SLL == 1}]]
    dict for {resource segment} [dict get $record segments] {
        if {$resource ni $actual} {error "CARRY preroute missed its planned SLL: $resource"}
    }
    set f [open [file join $out carry-preroute-status.tsv] w]
    puts $f "net\tstatus\tseconds\tplanned_sll\tactual_sll"
    puts $f [join [list $name $status $seconds [dict keys [dict get $record segments]] $actual] "\t"]
    close $f
    puts "FULL_CARRY_PREROUTE_COMPLETED status=$status seconds=$seconds"
}
