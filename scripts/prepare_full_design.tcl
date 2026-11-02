# Freeze physical interfaces from the input or an explicitly supplied placed reference.
# Fabric placement is never copied from the reference.
if {$argc < 2 || $argc > 3} {error "Expected input.dcp new-directory ?physical-reference.dcp?"}
lassign $argv input output reference
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
source [file join [file dirname [info script]] full_design_interfaces.tcl]
set_param general.maxThreads 4
if {[catch {
    set assignments [dict create]
    if {$reference ne ""} {
        set mapping [file join $output reference-interface-map.tsv]
        if {[file extension $reference] eq ".tsv"} {
            file copy $reference $mapping
        } else {
            open_checkpoint $reference
            set referencePart [get_property PART [current_design]]
            set f [open $mapping w]
            puts $f "#part\t$referencePart"
            puts $f "#source\t[file normalize $reference]"
            puts $f "#sha256\t[lindex [exec sha256sum -- $reference] 0]"
            set canonical [amf_full::canonical_interfaces]
            foreach c $canonical kind [amf_full::properties REF_NAME $canonical] {
                puts $f [join [list [get_property NAME $c] $kind [get_property LOC $c] [get_property BEL $c]] "\t"]
            }
            close $f
            close_project
        }
        # Reload plain strings: Vivado cell handles do not survive close_project.
        set f [open $mapping r]
        gets $f line;lassign [split $line "\t"] key referencePart
        if {$key ne "#part"} {error "Invalid reference map part header"}
        gets $f line;lassign [split $line "\t"] key referenceSource
        if {$key ne "#source"} {error "Invalid reference map source header"}
        gets $f line;lassign [split $line "\t"] key referenceHash
        if {$key ne "#sha256" || [lindex [exec sha256sum -- $referenceSource] 0] ne $referenceHash} {error "Reference map DCP binding mismatch"}
        while {[gets $f line] >= 0} {
            lassign [split $line "\t"] name kind site bel
            if {[dict exists $assignments $name]} {error "Duplicate reference interface $name"}
            dict set assignments $name [list $kind $site $bel]
        }
        close $f
        puts "REFERENCE_INTERFACE_MAP [dict size $assignments]";flush stdout
    }
    open_checkpoint $input
    set part [get_property PART [current_design]]
    if {![string match xcu250-* $part]} {error "No device adapter for $part"}
    if {$reference ne "" && $referencePart ne $part} {error "Physical reference part mismatch"}
    if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Black boxes in input"}
    # A future case with an additional fixed fabric macro must fail explicitly;
    # this adapter must not silently release ownership it cannot represent.
    foreach cell [get_cells -hier -quiet -filter {IS_LOC_FIXED || IS_BEL_FIXED}] {
        set name [get_property NAME $cell];set parent $name;set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            set ancestor [get_cells -quiet $parent]
            if {[llength $ancestor] && [get_property IS_PRIMITIVE $ancestor]} {set internal 1;break}
        }
        if {!$internal && ![amf_full::interface [get_property REF_NAME $cell]]} {
            error "Additional fixed fabric cell requires an explicit ownership adapter: $name"
        }
    }
    if {$reference eq ""} {place_ports}
    set f [open [file join $output fixed_cells.tsv] w]
    puts $f "cell\tprimitive\tsite\tbel\tfixed\ttarget_mode"
    set u [open [file join $output fixed_units] w]
    puts $u "# Fixed physical interfaces; composite targets are replayed at site granularity"
    set occ [open [file join $output fixed_occupancy.tsv] w]
    puts $occ "cell\tsite\tbel"
    set sites {};set count 0
    set canonical [amf_full::canonical_interfaces]
    foreach c $canonical kind [amf_full::properties REF_NAME $canonical] {
        if {![amf_full::interface $kind]} {continue}
        set name [get_property NAME $c]
        set site [get_property LOC $c];set bel [get_property BEL $c]
        if {$reference ne ""} {
            if {![dict exists $assignments $name]} {error "Physical reference missing interface $c"}
            lassign [dict get $assignments $name] refKind refSite refBel
            if {$refKind ne $kind} {error "Physical reference type mismatch: $c"}
            if {[amf_full::bool_prop $c IS_LOC_FIXED] && $site ne $refSite} {error "Reference conflicts with fixed LOC: $c"}
            if {[amf_full::bool_prop $c IS_BEL_FIXED] && $bel ne $refBel} {error "Reference conflicts with fixed BEL: $c"}
            set site $refSite;set bel $refBel
        }
        set bel [lindex [split $bel .] end]
        if {$site eq "" || $bel eq ""} {error "Missing physical assignment for $c ($kind); supply a placed reference"}
        set mode [amf_full::target_mode $kind]
        set target $site
        if {$mode eq "bel"} {set target "$site/$bel"}
        place_cell [list $c $target]
        set_property IS_LOC_FIXED true $c
        if {$mode eq "bel"} {set_property IS_BEL_FIXED true $c}
        if {[get_property LOC $c] ne $site} {error "Interface LOC replay failed: $c"}
        puts $f "$c\t$kind\t$site\t$bel\t1\t$mode"
        puts $u "name=> $c loc=> $site bel=> $site/$bel"
        lappend sites $site
        foreach occupied [get_bels -of_objects $c] {
            set occupiedSite [get_sites -of_objects $occupied]
            puts $occ "$c\t$occupiedSite\t$occupied"
            lappend sites {*}$occupiedSite
        }
        incr count
    }
    close $f;close $u;close $occ
    # Related I/O placement may clear PHY BEL locks; apply locks after all assignments.
    foreach c $canonical {
        set_property IS_LOC_FIXED true $c
        if {[amf_full::target_mode [get_property REF_NAME $c]] eq "bel"} {
            set_property IS_BEL_FIXED true $c
        }
    }
    set f [open [file join $output fixed_sites.tsv] w]
    puts $f "site\ttile\tclock_region\tsite_type\ttile_type\trpm_x\trpm_y\tslr\tprohibited\tbels"
    foreach name [lsort -unique $sites] {
        set site [get_sites $name];set tile [get_tiles -of_objects $site]
        if {![regexp {^SLR([0-9]+)$} [get_slrs -of_objects $site] -> sid]} {error "Missing SLR: $site"}
        puts $f [join [list $site $tile [get_property CLOCK_REGION $site] [get_property SITE_TYPE $site] [get_property TYPE $tile] [get_property RPM_X $site] [get_property RPM_Y $site] $sid [expr {[get_property PROHIBIT $site]?1:0}] [join [get_bels -of_objects $site] ,]] "\t"]
    }
    close $f
    write_xdc [file join $output constraints.xdc]
    write_checkpoint [file join $output prepared.dcp]
    puts "AMF_INTERFACES_PREPARED $count"
    # Export from the already open design, bound to the checkpoint just written.
    set argv [list [file join $output prepared.dcp] [file join $output inventory]]
    set argc 2
    set ::amf_full_current_design 1
    source [file join [file dirname [info script]] export_full_inventory.tcl]
} msg opts]} {
    puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1
}
exit 0
