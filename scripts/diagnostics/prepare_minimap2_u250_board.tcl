# Prepare real board I/O and clock resources before the AMF fabric placement.
if {$argc != 2} {error "Expected post_opt.dcp and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
set timeline [open [file join $output stages.tsv] w]
puts $timeline "stage\tseconds"
proc timed {name body} {
    global timeline
    puts "BOARD_STAGE_BEGIN $name"; flush stdout
    set begin [clock milliseconds]
    uplevel 1 $body
    puts $timeline "$name\t[expr {([clock milliseconds]-$begin)/1000.0}]";flush $timeline
    puts "BOARD_STAGE_END $name";flush stdout
}
if {[catch {
    timed open {open_checkpoint $input}
    # U250 official board preset: LVCMOS12 user LEDs require DRIVE 8.
    set_property DRIVE 8 [get_ports {led[*]}]
    if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {error "Wrong part"}
    if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Black boxes in board design"}
    if {[llength [get_cells -hier -quiet -filter {REF_NAME == PCIE40E4}]] != 1} {error "Expected one PCIe hard core"}
    if {[llength [get_cells -hier -quiet -filter {REF_NAME == GTYE4_CHANNEL}]] != 4} {error "Expected four PCIe lanes"}
    foreach port [get_ports] {
        if {[get_property PACKAGE_PIN $port] eq ""} {error "Board port without package pin: $port"}
    }
    timed io_clock_placement {place_ports}
    set primitives [get_cells -hier -filter {IS_PRIMITIVE}]
    array set primitive_names {}
    foreach name [get_property NAME $primitives] {set primitive_names($name) 1}
    set fabric_types {LUT1 LUT2 LUT3 LUT4 LUT5 LUT6 LUT6_2 FDRE FDSE FDCE FDPE LDCE AND2B1L CARRY8 DSP48E2 MUXF7 MUXF8 SRL16E SRLC32E RAM32M16 RAM64M RAM64X1D RAM32M RAM32X1D RAM32X1S RAM64X1S RAM64M8 RAM256X1D RAM256X1S FIFO36E2 FIFO18E2 RAMB18E2 RAMB36E2 URAM288 URAM288_BASE}
    set fixed [open [file join $output fixed_cells.tsv] w]
    puts $fixed "cell\tprimitive\tsite\tbel\tfixed"
    set units [open [file join $output fixed_units] w]
    puts $units "# Fixed board interfaces and clocks"
    set missing {}
    set site_names {}
    set fixed_names {}
    foreach cell $primitives {
        set name [get_property NAME $cell]
        set parent $name;set internal 0
        while {[set slash [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$slash-1}]]
            if {[info exists primitive_names($parent)]} {set internal 1;break}
        }
        if {$internal} {continue}
        set type [get_property REF_NAME $cell]
        if {$type in $fabric_types || $type in {GND VCC}} {continue}
        set site [get_property LOC $cell];set bel [lindex [split [get_property BEL $cell] .] end]
        puts $fixed "$name\t$type\t$site\t$bel\t[get_property IS_LOC_FIXED $cell]"
        if {$site eq "" || $bel eq ""} {lappend missing $name;continue}
        # IBUF/OBUF are composite I/O macros; Vivado accepts their site, while
        # their displayed internal BEL is not a legal place_cell target.
        if {$type in {IBUF OBUF}} {
            place_cell [list $name $site]
        } else {
            place_cell [list $name $site/$bel]
            set_property IS_BEL_FIXED true $cell
        }
        puts $units "name=> $name loc=> $site bel=> $site/$bel"
        lappend site_names $site
        lappend fixed_names $name
    }
    close $fixed;close $units
    if {[llength $missing]} {error "Unplaced fixed/interface resources after place_ports: $missing"}
    set_property IS_LOC_FIXED true [get_cells $fixed_names]
    set f [open [file join $output fixed_sites.tsv] w]
    puts $f "site\ttile\tclock_region\tsite_type\ttile_type\trpm_x\trpm_y\tslr\tprohibited\tbels"
    foreach name [lsort -unique $site_names] {
        set site [get_sites $name];set tile [get_tiles -of_objects $site]
        set slr [get_slrs -of_objects $site]
        if {![regexp {^SLR([0-9]+)$} $slr -> sid]} {error "Missing SLR for $site"}
        puts $f [join [list $site $tile [get_property CLOCK_REGION $site] [get_property SITE_TYPE $site] [get_property TYPE $tile] [get_property RPM_X $site] [get_property RPM_Y $site] $sid [expr {[get_property PROHIBIT $site]?1:0}] [join [get_bels -of_objects $site] ,]] "\t"]
    }
    close $f
    report_clocks -file [file join $output clocks.rpt]
    report_io -file [file join $output io.rpt]
    check_timing -verbose -file [file join $output check_timing.rpt]
    write_xdc [file join $output constraints.xdc]
    timed checkpoint {write_checkpoint [file join $output board_prepared.dcp]}
    puts "MINIMAP2_U250_BOARD_PREPARED fixed=[llength $fixed_names]"
} msg opts]} {
    puts stderr $msg;puts stderr [dict get $opts -errorinfo]
    close $timeline;exit 1
}
close $timeline
exit 0
