# Read-only checkpoint inspection. No implementation commands or DCP writes.
if {$argc != 2} {error "Expected input DCP and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    if {[get_property PART [current_design]] ne "xcu250-figd2104-2L-e"} {error "Unexpected part"}
    set f [open [file join $output metadata.tsv] w]
    puts $f "input_sha256\t[lindex [exec sha256sum -- $input] 0]"
    puts $f "vivado_version\t[version -short]"
    puts $f "part\t[get_property PART [current_design]]"
    puts $f "blackboxes\t[llength [get_cells -quiet -hier -filter {IS_BLACKBOX}]]"
    close $f
    set f [open [file join $output cells.tsv] w]
    puts $f "name\tref_name"
    set cells [get_cells -hier -filter {IS_PRIMITIVE}]
    foreach name [get_property NAME $cells] ref [get_property REF_NAME $cells] {
        puts $f "$name\t$ref"
    }
    close $f
    set f [open [file join $output ports.tsv] w]
    puts $f "name\tdirection\tpackage_pin\tiostandard"
    foreach p [lsort [get_ports]] {
        puts $f "$p\t[get_property DIRECTION $p]\t[get_property PACKAGE_PIN $p]\t[get_property IOSTANDARD $p]"
    }
    close $f
    set f [open [file join $output clocks.tsv] w]
    puts $f "name\tperiod\twaveform\tsource_pins"
    foreach c [lsort [get_clocks]] {
        puts $f "$c\t[get_property PERIOD $c]\t[get_property WAVEFORM $c]\t[get_property SOURCE_PINS $c]"
    }
    close $f
    write_verilog -mode funcsim -rename_top minimap2_functional [file join $output functional.v]
    puts "FUNCTIONAL_NETLIST_EXPORTED"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
