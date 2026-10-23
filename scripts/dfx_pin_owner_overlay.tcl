# Enrich only ownership classes relevant to a replacement RM. An internal
# static signal cannot be the same net as an RM signal unless it crosses an RP
# interface. Constant polarity and every RP interface owner are resolved exactly.
namespace eval dfx_pin_owner_overlay {
    proc names {objects} {
        if {![llength $objects]} {return {}}
        return [get_property NAME $objects]
    }
    proc intersection {nets wanted} {
        set result {}
        if {![llength $nets]} {return $result}
        set nodes [get_nodes -of_objects $nets]
        foreach node [names $nodes] {
            if {[dict exists $wanted $node]} {dict set result $node 1}
        }
        return $result
    }
    proc collect {occupancy output rps} {
        set started [clock microseconds]
        set f [open $occupancy r]
        if {[gets $f] ne "node\tkind"} {error "Unexpected occupancy schema"}
        set wanted {}
        while {[gets $f row]>=0} {
            lassign [split $row \t] node kind
            if {$kind ni {static boundary} || [dict exists $wanted $node]} {error "Invalid occupancy"}
            dict set wanted $node $kind
        };close $f
        set aliases {};set netObjects {}
        foreach row $rps {
            lassign $row module rp pb
            set cell [get_cells -quiet $rp]
            if {[llength $cell]!=1 || ![get_property IS_BLACKBOX $cell]} {error "Expected empty RP $module"}
            foreach pin [get_pins -of_objects $cell] {
                set port [string range [get_property NAME $pin] [expr {[string length $rp]+1}] end]
                set nets [get_nets -quiet -top_net_of_hierarchical_group -segments -of_objects $pin]
                foreach net $nets name [names $nets] {
                    dict set netObjects $name $net
                    dict lappend aliases $name "$module/$port"
                }
            }
        }
        puts "AMF_PIN_OWNER_STAGE canonical_interfaces count=[dict size $netObjects] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        set candidates [dict values $netObjects];set types {};set fixed {};set loose {}
        foreach net $candidates name [names $candidates] type [get_property TYPE $candidates] locked [get_property IS_ROUTE_FIXED $candidates] {
            dict set types $name $type
            if {$locked} {lappend fixed $net} else {lappend loose $net}
        }
        if {[dict size [intersection $loose $wanted]]} {error "Nonfixed boundary owner in certified occupancy"}
        puts "AMF_PIN_OWNER_STAGE interface_properties elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        set owners {};set signals {};set direct {}
        foreach net $fixed name [names $fixed] {
            if {[dict get $types $name] in {GLOBAL_CLOCK CLOCK}} {
                continue
            } elseif {[dict get $types $name] in {GROUND POWER}} {
                lappend direct $net
            } else {lappend signals $net}
        }
        foreach net $direct name [names $direct] {
            dict for {node unused} [intersection [list $net] $wanted] {
                dict set owners $node [list $name [dict get $types $name] [join [lsort -unique [dict get $aliases $name]] |]]
            }
        }
        # Encode canonical signal-net IDs through logarithmically many bulk
        # route queries. Complement queries verify unique physical ownership.
        set union [intersection $signals $wanted];set codes {}
        puts "AMF_PIN_OWNER_STAGE signal_union nodes=[dict size $union] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        set count [llength $signals];set signalNames [names $signals]
        for {set bit 0} {(1<<$bit)<=$count} {incr bit} {
            set yes {};set no {};set index 1
            foreach net $signals {
                if {$index & (1<<$bit)} {lappend yes $net} else {lappend no $net}
                incr index
            }
            set y [intersection $yes $union];set n [intersection $no $union]
            dict for {node unused} $union {
                set yy [dict exists $y $node];set nn [dict exists $n $node]
                if {$yy==$nn} {error "Missing or overlapping canonical owner: $node"}
                if {![dict exists $codes $node]} {dict set codes $node 0}
                if {$yy} {dict set codes $node [expr {[dict get $codes $node] | (1<<$bit)}]}
            }
            puts "AMF_PIN_OWNER_STAGE owner_bit bit=$bit nets=$count nodes=[dict size $union] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        }
        dict for {node code} $codes {
            if {$code<1 || $code>$count || [dict exists $owners $node]} {error "Invalid canonical owner"}
            set name [lindex $signalNames [expr {$code-1}]]
            dict set owners $node [list $name [dict get $types $name] [join [lsort -unique [dict get $aliases $name]] |]]
        }
        puts "AMF_PIN_OWNER_STAGE boundary_complete nodes=[dict size $owners] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        # Clock pins are outside local data-pin policy; no global clock-tree enumeration.
        # Every cached boundary node must nevertheless resolve below, or fail.
        # Constant ownership needs its physical polarity, not the name
        # of an individual hierarchy segment.
        set tilesWanted {};dict for {node kind} $wanted {dict set tilesWanted [lindex [split $node /] 0] 1}
        set selectedTiles {};set tileObjects [get_tiles]
        foreach tile $tileObjects name [names $tileObjects] {if {[dict exists $tilesWanted $name]} {lappend selectedTiles $tile}}
        if {[llength $selectedTiles]!=[dict size $tilesWanted]} {error "Incomplete occupied tile scope"}
        puts "AMF_PIN_OWNER_STAGE constant_tiles count=[llength $selectedTiles] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        set candidates [get_nets -of_objects $selectedTiles]
        set constants [filter $candidates {TYPE == GROUND || TYPE == POWER}]
        puts "AMF_PIN_OWNER_STAGE constant_candidates count=[llength $constants] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
        foreach type {GROUND POWER} {
            set group [filter $constants "TYPE == $type"]
            puts "AMF_PIN_OWNER_STAGE constant_type type=$type count=[llength $group] elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
            dict for {node unused} [intersection $group $wanted] {
                if {[dict exists $owners $node]} {
                    if {[lindex [dict get $owners $node] 1] ne $type} {error "Constant/signal ownership conflict"}
                } else {dict set owners $node [list "<fixed-$type>" $type {}]}
            }
        }
        set resolved [dict size $owners];set coarse 0
        set f [open $output w];fconfigure $f -buffersize 1048576
        puts $f "node\tnet\ttype\tports\tfixed"
        dict for {node kind} $wanted {
            if {![dict exists $owners $node]} {
                if {$kind eq "boundary"} {error "Missing cached boundary owner: $node"}
                dict set owners $node [list <fixed-static-nonboundary> OTHER_STATIC {}];incr coarse
            }
            puts $f [join [linsert [dict get $owners $node] 0 $node] \t]\t1
        };close $f
        set f [open [file join [file dirname $output] ownership_scope.tsv] w]
        puts $f "exact_boundary_or_constant_nodes\t$resolved\nnonboundary_nonconstant_fixed_nodes\t$coarse\nall_occupied_nodes\t[dict size $wanted]\ninternal_static_net_names_required\t0\ncomplete\t1";close $f
        puts "AMF_PIN_OWNERS_COMPLETE nodes=[dict size $wanted] exact=$resolved fixed_nonboundary=$coarse elapsed=[expr {([clock microseconds]-$started)/1e6}]";flush stdout
    }
}

