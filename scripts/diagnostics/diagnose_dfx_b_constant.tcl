set out [lindex $argv 0]
set cases [lrange $argv 1 end]
file mkdir $out
set_param general.maxThreads 4
proc emit {f kind obj prop value} {
    set value [string map [list "\t" {\t} "\n" {\n} "\r" {}] $value]
    puts $f "$kind\t$obj\t$prop\t$value"
}
proc props {f kind obj keys} {
    set available [list_property $obj]
    foreach p $keys {
        if {[lsearch -exact $available $p] >= 0} {emit $f $kind $obj $p [get_property $p $obj]}
    }
}
proc query {script} {
    if {[catch {uplevel 1 $script} result]} {return "QUERY_ERROR: $result"}
    return $result
}
proc snapshot {tag} {
    global out
    set f [open $out/$tag.tsv w]
    puts $f "kind\tobject\tproperty\tvalue"
    foreach siteName {DSP48E2_X11Y120 DSP48E2_X11Y121} {
        set site [get_sites $siteName]
        emit $f site $site clock_region [get_clock_regions -of_objects $site]
        emit $f site $site slr [get_slrs -of_objects $site]
        foreach c [get_cells -quiet -of_objects $site] {
            report_property -all -file $out/$tag-$siteName-cell.rpt $c
            props $f cell $c {REF_NAME LOC BEL IS_LOC_FIXED IS_BEL_FIXED IS_PRIMITIVE DONT_TOUCH}
            foreach pin [get_pins -of_objects $c] {
                emit $f pin $pin net [get_nets -quiet -of_objects $pin]
                emit $f pin $pin site_pins [get_site_pins -quiet -of_objects $pin]
            }
        }
        foreach suffix {DIN0 DIN3} {
            set sp [get_site_pins -quiet $siteName/$suffix]
            if {![llength $sp]} {continue}
            emit $f site_pin $sp nets [query {get_nets -quiet -of_objects $sp}]
            emit $f site_pin $sp pins [query {get_pins -quiet -of_objects $sp}]
            set nodes [get_nodes -quiet -of_objects $sp]
            emit $f site_pin $sp nodes $nodes
            foreach node $nodes {
                emit $f node $node nets [get_nets -quiet -of_objects $node]
                emit $f node $node wires [get_wires -quiet -of_objects $node]
                emit $f node $node uphill [get_nodes -quiet -uphill -of_objects $node]
            }
        }
    }
    set prefix {kernel/grp_getrf_core_double_2_512_256_s_fu_1411/grp_subUpdate_double_2_512_s_fu_7178/grp_subUpdate_double_2_512_Pipeline_LoopMulSub_fu_44/dmul_64ns_64ns_64_5_max_dsp_1_U1564}
    foreach suffix {{kernel_getrf_0_dmul_64ns_64ns_64_5_max_dsp_1_ip_u/U0/i_synth/MULT.OP/i_non_prim.MULT/DSP48E2_SPD_DBL_VARIANT.FIX_MULT/FULL_MAX_USAGE.DSP_SIMD/DSP/P[44]} {din0_buf1[23]}} {
        set name $prefix/$suffix
        set n [get_nets -hierarchical -quiet -filter [list NAME == $name]]
        emit $f target_net $name found [llength $n]
        if {[llength $n]} {
            props $f net $n {TYPE ROUTE_STATUS IS_ROUTE_FIXED IS_FIXED IS_BOUNDARY FIXED_ROUTE ROUTE}
            emit $f net $n pins [get_pins -quiet -of_objects $n]
            emit $f net $n site_pins [get_site_pins -quiet -of_objects $n]
            emit $f net $n nodes [lsort [get_nodes -quiet -of_objects $n]]
            emit $f net $n pips [lsort [get_pips -quiet -of_objects $n]]
        }
    }
    foreach pb [get_pblocks -quiet] {props $f pblock $pb {GRID_RANGES CONTAIN_ROUTING EXCLUDE_PLACEMENT IS_SOFT}}
    close $f
    puts "DIAG_SNAPSHOT_DONE $tag"
    flush stdout
}
set timing [open $out/times.tsv w]
puts $timing "stage\tseconds\texit_code"
if {[catch {
    foreach pair $cases {
        lassign $pair tag dcp
        set begin [clock milliseconds]
        open_checkpoint $dcp
        puts $timing "open_$tag\t[expr {([clock milliseconds]-$begin)/1000.0}]\t0"; flush $timing
        snapshot $tag
        if {$tag eq "reference"} {
            update_design -cell kernel/grp_getrf_core_double_2_512_256_s_fu_1411/grp_subUpdate_double_2_512_s_fu_6548 -black_box
            snapshot reference_blackboxed
            lock_design -level routing
            snapshot reference_locked
        }
        close_design
    }
} msg opts]} {
    close $timing
    set f [open $out/failure.txt w]; puts $f $msg; puts $f [dict get $opts -errorinfo]; close $f
    puts stderr [dict get $opts -errorinfo]
    exit 2
}
close $timing
set f [open $out/completed.txt w]; puts $f passed; close $f
exit 0
