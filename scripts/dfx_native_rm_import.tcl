# Apply only AMF coordinates to the optimized standalone RM. No static DCP is
# opened here; both resulting modules are later imported into one locked shell.
source [lindex $argv 0]
source [file join [file dirname [info script]] dfx_common.tcl]
source [file join [file dirname [info script]] dfx_site_import.tcl]
source [file join [file dirname [info script]] dfx_native_export.tcl]
source [file join [file dirname [info script]] dfx_native_interface_buffers.tcl]
source [file join [file dirname [info script]] dfx_ooc_clock_handoff.tcl]
source [file join [file dirname [info script]] dfx_native_lut_pins.tcl]
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    amf_dfx::timed open_optimized_ooc {open_checkpoint $dfx(rm_checkpoint)}
    amf_dfx::timed materialize_amf_interface_buffers {
        set buffers [amf_native_buffers::apply $out/interface_buffers.tsv $dfx(module_id)]
        puts "AMF_NATIVE_INTERFACE_BUFFERS_EXACT cells=$buffers INIT=2'h2 internal_fanout_preserved=1"
    }
    set f [open $out/placement.tsv r];set requested [dict create];set names [dict create]
    set prefix "$dfx(active_partition)/"
    while {[gets $f line]>=0} {
        lassign [split $line \t] full target
        if {[string first $prefix $full]!=0} {error "Placement outside active RM: $full"}
        set local [string range $full [string length $prefix] end]
        if {[dict exists $requested $local]} {error "Duplicate requested cell"}
        dict set requested $local $target;dict set names $local $full
    };close $f
    set actual {}
    foreach c [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}] {
        set t [get_property REF_NAME $c]
        if {$t in {GND VCC}} {continue}
        if {![amf_native_export::fabric $t]} {error "Unmodeled OOC primitive $c $t"}
        lappend actual [get_property NAME $c]
    }
    if {[lsort $actual] ne [lsort [dict keys $requested]]} {error "Incomplete/extra AMF module placement"}
    amf_dfx::timed reset_ooc_lut_groups {
        set log [open $out/import_group_reset.tsv w];puts $log "cell\tproperty\tprevious_value"
        foreach c [get_cells -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME =~ LUT*}] {
            foreach property {HLUTNM SOFT_HLUTNM} {
                set v [get_property $property $c]
                if {$v ne ""} {puts $log "$c\t$property\t$v";set_property $property {} $c}
            }
        };close $log
    }
    amf_dfx::timed import_amf_placement {amf_site_import::place $requested}
    if {[info exists dfx(native_local_pin_policy)] && $dfx(native_local_pin_policy)} {
        amf_dfx::timed import_amf_lut_pins {amf_native_lut_pins::apply $out/lut_pin_assignments.tsv $prefix;amf_native_lut_pins::verify $out/lut_pin_assignments.tsv $prefix {} module_import}
    }
    foreach {name variable} {allowed_sites allowed blocked_sites blocked} {
        set f [open $out/$name.txt r];set $variable [dict create]
        foreach s [split [string trim [read $f]] \n] {if {$s ne ""} {dict set $variable $s 1}};close $f
    }
    set f [open $out/import_retention.tsv w];puts $f "cell\trequested\tloc\tbel"
    dict for {name target} $requested {
        set c [get_cells [list $name]];set loc [get_property LOC $c];set bel [lindex [split [get_property BEL $c] .] end]
        set actual $loc;if {[string first / $target]>=0} {append actual /$bel}
        puts $f "[dict get $names $name]\t$target\t$loc\t$bel"
        if {$actual ne $target || ![dict exists $allowed $loc] || [dict exists $blocked $loc]} {error "Native import LOC/BEL/region mismatch: $name"}
    };close $f
    if {[info exists dfx(clock_handoff_mode)] && $dfx(clock_handoff_mode) eq "inherit_static_primary"} {
        amf_dfx::timed strip_handoff_primary {amf_ooc_clock::strip_checked_primary}
    }
    amf_dfx::timed write_rm_amf_placed {write_checkpoint $out/rm_amf_placed.dcp}
    puts "AMF_NATIVE_OOC_IMPORT_EXACT cells=[dict size $requested] place_design_executed=0"
}
if {[catch {main} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 2}
exit 0
