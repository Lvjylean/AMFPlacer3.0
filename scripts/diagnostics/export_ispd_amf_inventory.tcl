# Export canonical primitive boundaries and complete hierarchical net groups.
# Internal Unisim implementation pins are excluded using the canonical leaf set.
if {$argc != 2} {error "Expected prepared.dcp and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
proc properties {property objects} {
    if {![llength $objects]} {return {}}
    return [get_property $property $objects]
}
if {[catch {
    open_checkpoint $input
    set fixed [get_cells -hier -filter {REF_NAME == IBUF || REF_NAME == OBUF || REF_NAME == BUFGCE}]
    set_property IS_LOC_FIXED true $fixed
    # IBUF/OBUF macro cells cannot all be marked IS_BEL_FIXED; preserve their
    # actual BEL names in fixed_units and audit them after backend import.
    write_checkpoint [file join $output amf_input.dcp]
    set begin [clock milliseconds]
    set primitives [get_cells -hier -filter {IS_PRIMITIVE}]
    array set primitive_names {}
    foreach name [get_property NAME $primitives] {set primitive_names($name) 1}
    array set cell_ids {}
    array set cell_types {}
    set cells [open [file join $output cells.tsv] w]
    puts $cells "cell_id\tcell_name\tprimitive"
    set id 0
    foreach name [get_property NAME $primitives] kind [get_property REF_NAME $primitives] {
        set parent $name
        set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            if {[info exists primitive_names($parent)]} {set internal 1;break}
        }
        if {$internal} {continue}
        if {[regexp {[\s,]} $name]} {error "Unsupported delimiter in cell name: $name"}
        set cell_ids($name) $id
        set cell_types($name) $kind
        puts $cells "$id\t$name\t$kind"
        incr id
    }
    close $cells
    puts "ISPD_CANONICAL_CELLS $id";flush stdout
    set nets [open [file join $output nets.tsv] w]
    puts $nets "net_id\tdrivers\tsinks\ttype"
    set count 0
    set omitted 0
    set visited 0
    set pin_count 0
    foreach net [get_nets -hierarchical -top_net_of_hierarchical_group] {
        incr visited
        set segments [get_nets -quiet -segments $net]
        if {![llength $segments]} {error "Cannot resolve hierarchical net: $net"}
        set pins [get_pins -quiet -of_objects $segments]
        set drivers {}
        set sinks {}
        set constants {}
        set kind SIGNAL
        foreach pin [properties NAME $pins] direction [properties DIRECTION $pins] {
            set split [string last / $pin]
            set cell [string range $pin 0 [expr {$split-1}]]
            if {![info exists cell_ids($cell)]} {continue}
            set endpoint "$cell_ids($cell):[string range $pin [expr {$split+1}] end]"
            if {$direction eq "OUT"} {
                lappend drivers $endpoint
                lappend constants $cell_types($cell)
                if {$cell_types($cell) eq "BUFGCE"} {set kind CLOCK}
            } elseif {$direction eq "IN"} {
                lappend sinks $endpoint
            } else {error "Unsupported bidirectional primitive pin: $pin"}
        }
        set ports [get_ports -quiet -of_objects $segments]
        foreach port [properties NAME $ports] direction [properties DIRECTION $ports] {
            if {$direction eq "IN"} {
                lappend drivers "PORT:$port"
                lappend constants PORT
            } elseif {$direction eq "OUT"} {
                lappend sinks "PORT:$port"
            } else {error "Unsupported bidirectional port: $port"}
        }
        set drivers [lsort -unique $drivers]
        set sinks [lsort -unique $sinks]
        if {![llength $drivers] && ![llength $sinks]} {incr omitted;continue}
        if {[llength $drivers]>1 && [llength [lsort -unique $constants]]==1 && [lindex $constants 0] in {GND VCC}} {
            # Equivalent constant sources are represented by one constant driver.
            set drivers [lrange $drivers 0 0]
        }
        if {[llength $drivers]!=1} {error "Non-single-driver logical net $net : $drivers -> $sinks"}
        puts $nets "n$count\t[join $drivers ,]\t[join $sinks ,]\t$kind"
        incr count
        incr pin_count [expr {[llength $drivers]+[llength $sinks]}]
        if {$count % 20000 == 0} {puts "ISPD_NET_EXPORT $count visited=$visited";flush stdout}
    }
    close $nets
    if {$count == 0 || $pin_count < $id} {error "Incomplete connectivity inventory: cells=$id nets=$count endpoints=$pin_count"}
    set fixed [get_cells -hier -filter {REF_NAME == IBUF || REF_NAME == OBUF || REF_NAME == BUFGCE}]
    set f [open [file join $output fixed_units] w]
    puts $f "# Fixed input/output and clock buffer locations prepared by Vivado"
    set sites {}
    foreach name [get_property NAME $fixed] site [get_property LOC $fixed] bel [get_property BEL $fixed] {
        if {$site eq "" || $bel eq ""} {error "Unplaced fixed boundary cell $name"}
        puts $f "name=> $name loc=> $site bel=> $bel"
        lappend sites $site
    }
    close $f
    set f [open [file join $output fixed_sites.tsv] w]
    puts $f "site\ttile\tclock_region\tsite_type\ttile_type\trpm_x\trpm_y\tslr\tprohibited\tbels"
    foreach site_name [lsort -unique $sites] {
        set site [get_sites $site_name]
        set tile [get_tiles -of_objects $site]
        set slr [get_slrs -of_objects $site]
        if {![regexp {^SLR([0-9]+)$} $slr -> slr_id]} {error "Invalid SLR for $site"}
        puts $f [join [list $site $tile [get_property CLOCK_REGION $site] [get_property SITE_TYPE $site] [get_property TYPE $tile] [get_property RPM_X $site] [get_property RPM_Y $site] $slr_id [expr {[get_property PROHIBIT $site]?1:0}] [join [get_bels -of_objects $site] ,]] "\t"]
    }
    close $f
    set f [open [file join $output export_summary.tsv] w]
    puts $f "canonical_cells\t$id"
    puts $f "logical_nets\t$count"
    puts $f "endpoint_count\t$pin_count"
    puts $f "omitted_internal_nets\t$omitted"
    puts $f "export_seconds\t[expr {([clock milliseconds]-$begin)/1000.0}]"
    close $f
    puts "ISPD_AMF_INVENTORY_EXPORTED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
