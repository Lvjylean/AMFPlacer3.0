# Reuse only immutable device topology. Query route owners from the currently
# open, locked, empty-RP static shell; no placement/routing/property mutations.
namespace eval amf_pin_access {
    proc names {objects} {if {![llength $objects]} {return {}};return [get_property NAME $objects]}
    proc check_budget {} {
        variable started;global dfx
        if {([clock microseconds]-$started)/1e6+$dfx(native_pin_preflight_seconds)>$dfx(native_pin_cache_budget_seconds)} {
            error "Automatic pin-cache generation exceeded its budget"
        }
    }
    proc collect {} {
        global dfx;variable started
        set started [clock microseconds]
        set out $dfx(output)/native_cache/pin_access
        if {[file exists $out]} {error "Refusing to overwrite pin-access generation"}
        file mkdir $out
        set f [open $out/started_epoch.txt w];puts $f [expr {$started/1e6}];close $f
        set topology $dfx(native_pin_topology_root)
        if {[version -short] ne $dfx(native_pin_topology_vivado)} {error "Device topology Vivado version mismatch"}
        if {[get_property PART [current_design]] ne $dfx(part)} {error "Device topology part mismatch"}
        array set boundaryNames {}
        amf_dfx::timed native_pin_scope_and_interfaces {
            foreach row $dfx(rps) {
                lassign $row id rp pb
                set cell [get_cells -quiet $rp]
                if {[llength $cell]!=1 || ![get_property IS_BLACKBOX $cell]} {error "Pin cache requires empty RP $id"}
                set actual [lsort -unique [names [filter [get_sites -of_objects [get_pblocks $pb]] {NAME =~ SLICE_* || NAME =~ DSP*}]]]
                set f [open $topology/${id}_sites.txt r];set expected [lsort [split [string trim [read $f]] \n]];close $f
                if {$actual ne $expected} {error "Immutable pin topology differs from actual RP scope $id"}
                foreach name [names [get_nets -segments -of_objects [get_pins -of_objects $cell]]] {set boundaryNames($name) 1}
            }
        }
        array set wanted {};array set wantedTiles {}
        amf_dfx::timed native_pin_topology_load {
            set f [open $topology/wanted_nodes.txt r]
            while {[gets $f name]>=0} {
                if {$name eq "" || [info exists wanted($name)]} {error "Invalid/duplicate candidate node"}
                set wanted($name) 1;set wantedTiles([lindex [split $name /] 0]) 1
            };close $f
        }
        set selectedTiles {}
        amf_dfx::timed native_pin_select_tiles {
            set objects [get_tiles]
            foreach tile $objects name [names $objects] {if {[info exists wantedTiles($name)]} {lappend selectedTiles $tile}}
            if {[llength $selectedTiles]!=[array size wantedTiles]} {error "Incomplete topology tile coverage"}
        }
        check_budget
        amf_dfx::timed native_pin_candidate_net_owners {set ns [get_nets -of_objects $selectedTiles]}
        set static {};set boundary {};set nonfixed {}
        foreach n $ns name [names $ns] fixed [get_property IS_ROUTE_FIXED $ns] {
            if {!$fixed} {lappend nonfixed $n
            } elseif {[info exists boundaryNames($name)]} {lappend boundary $n
            } else {lappend static $n}
        }
        set netCount [llength $ns];unset ns
        set f [open $out/occupied_nodes.tsv w];puts $f "node\tkind"
        set progress [open $out/progress.tsv w];puts $progress "kind\tnets_done\tnets_total\tmatched"
        set totalMatches 0
        amf_dfx::timed native_pin_route_intersection {
            foreach kind {static boundary nonfixed} {
                set netlist [set $kind];set count [llength $netlist];set matched 0
                for {set off 0} {$off<$count} {incr off 4096} {
                    check_budget
                    foreach name [names [get_nodes -of_objects [lrange $netlist $off [expr {$off+4095}]]]] {
                        if {[info exists wanted($name)]} {
                            if {$kind eq "nonfixed"} {error "Static pin topology has a nonfixed owner: $name"}
                            puts $f "$name\t$kind";incr matched
                        }
                    }
                    puts $progress "$kind\t[expr {min($off+4096,$count)}]\t$count\t$matched";flush $progress
                }
                incr totalMatches $matched
            }
        }
        close $f;close $progress;check_budget
        set f [open $out/identity.tsv w]
        puts $f "part\t[get_property PART [current_design]]\nvivado\t[version -short]"
        puts $f "candidate_nodes\t[array size wanted]\ntiles\t[llength $selectedTiles]\nqueried_nets\t$netCount\nmatched_rows\t$totalMatches"
        puts $f "seconds\t[expr {([clock microseconds]-$started)/1e6}]\ncomplete\t1"
        close $f
    }
}
