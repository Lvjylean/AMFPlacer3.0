# Full logical-object export through public Vivado queries, including protected IP.
# This does not decrypt or modify IP/netlists. Simulation-only parameters may be absent.
set_param general.maxThreads 2
lassign $argv root run output schemaFile stages nativeResumePins
if {$stages eq ""} {set stages {input routed}}
if {$nativeResumePins eq ""} {set nativeResumePins 0}
source $schemaFile
proc field {value} {
    return [string map [list "\\" "\\\\" "\t" "\\t" "\n" "\\n" "\r" "\\r"] $value]
}
proc export_native {directory} {
    global primitiveParameters nativeResumePins
    file mkdir $directory
    set cells [get_cells -hierarchical -filter {IS_PRIMITIVE}]
    if {!$nativeResumePins} {
    set groups [dict create]
    foreach cell $cells type [get_property REF_NAME $cells] {dict lappend groups $type $cell}
    set schema [open [file join $directory parameter-coverage.tsv] w]
    puts $schema "type\tparameter\tpresent\tcells"
    set f [open [file join $directory parameters.tsv] w]
    puts $f "cell\ttype\tparameter\tvalue"
    dict for {type members} $groups {
        if {![dict exists $primitiveParameters $type]} {error "Missing primitive parameter schema for $type"}
        set available [list_property [lindex $members 0]]
        report_property -all [lindex $members 0] -file [file join $directory "schema-$type.rpt"]
        foreach parameter [dict get $primitiveParameters $type] {
            set exists [expr {[lsearch -exact $available $parameter] >= 0}]
            puts $schema "$type\t$parameter\t$exists\t[llength $members]"
            if {!$exists} {continue}
            set values [get_property $parameter $members]
            # Vivado returns a scalar for one object, including the empty string.
            if {[llength $members] == 1} {set values [list $values]}
            if {[llength $values] != [llength $members]} {error "Incomplete parameter vector $type $parameter"}
            foreach cell $members value $values {puts $f "[field $cell]\t$type\t$parameter\t[field $value]"}
        }
        puts "NATIVE_PARAMETERS $type [llength $members]"; flush stdout
    }
    close $f; close $schema
    set f [open [file join $directory pins.tsv] w]
    puts $f "pin\tdirection\tconnected\tinverted\tref_pin"
    set pinCount 0
    for {set i 0} {$i < [llength $cells]} {incr i 10000} {
        set pins [get_pins -quiet -of_objects [lrange $cells $i [expr {$i+9999}]]]
        foreach pin $pins direction [get_property DIRECTION $pins] connected [get_property IS_CONNECTED $pins] inverted [get_property IS_INVERTED $pins] refpin [get_property REF_PIN_NAME $pins] {
            puts $f "[field $pin]\t$direction\t$connected\t$inverted\t[field $refpin]"
            incr pinCount
        }
        if {$i % 100000 == 0} {puts "NATIVE_PINS $i $pinCount"; flush stdout}
    }
    close $f
    } else {
        foreach required {parameters.tsv pins.tsv parameter-coverage.tsv} {
            if {![file exists [file join $directory $required]]} {error "Missing completed export $required"}
        }
        set pinCount [expr {[lindex [exec wc -l [file join $directory pins.tsv]] 0] - 1}]
        puts "NATIVE_REUSED_PARAMETERS_PINS $pinCount"; flush stdout
    }
    set f [open [file join $directory nets.tsv] w]
    puts $f "net\tpins..."
    set netCount 0
    foreach net [get_nets -hierarchical] {
        set pins [get_pins -quiet -of_objects $net]
        set names {}
        if {[llength $pins]} {set names [get_property NAME $pins]}
        set escaped [list [field $net]]
        foreach name $names {lappend escaped [field $name]}
        puts $f [join $escaped "\t"]
        incr netCount
        if {$netCount % 100000 == 0} {puts "NATIVE_NETS $netCount"; flush stdout}
    }
    close $f
    set f [open [file join $directory ports.tsv] w]
    puts $f "port\tdirection\tnets..."
    foreach port [get_ports] {
        set nets [get_nets -quiet -of_objects $port]
        set names {}
        if {[llength $nets]} {set names [get_property NAME $nets]}
        set row [list [field $port] [get_property DIRECTION $port]]
        foreach name $names {lappend row [field $name]}
        puts $f [join $row "\t"]
    }
    close $f
    set f [open [file join $directory complete.tsv] w]
    puts $f "primitive_cells\t[llength $cells]\npins\t$pinCount\nnets\t$netCount"
    close $f
}
if {[catch {
    foreach stage $stages {
        if {$stage eq "input"} {set dcp [file join $root data reference getrf-u250 post_opt.dcp]} else {set dcp [file join $run reports getrf_routed.dcp]}
        puts "NATIVE_OPEN $stage"; flush stdout
        open_checkpoint $dcp
        export_native [file join $output $stage]
        close_design
        puts "NATIVE_DONE $stage"; flush stdout
    }
} message options]} {
    puts stderr [dict get $options -errorinfo]
    exit 2
}
puts NATIVE_AUDIT_EXPORT_FINISHED
exit 0
