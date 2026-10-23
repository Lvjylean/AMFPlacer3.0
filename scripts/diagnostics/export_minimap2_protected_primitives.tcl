# Ordinary read-only Vivado inspection of canonical primitive properties/pins.
if {$argc != 3} {error "Expected DCP, output directory, model-property Tcl"}
lassign $argv input output models
if {[file exists $output]} {error "Output exists"}
file mkdir $output
set_param general.maxThreads 4
source $models
if {[catch {
    open_checkpoint $input
    set raw [get_cells -hier -filter {IS_PRIMITIVE && (NAME =~ *udma_wrapper/* || NAME =~ *ram_top/*)}]
    array set names {}
    foreach n [get_property NAME $raw] {set names($n) 1}
    set cf [open [file join $output cells.tsv] w]
    set pf [open [file join $output parameters.tsv] w]
    set nf [open [file join $output pins.tsv] w]
    puts $cf "cell\ttype";puts $pf "cell\tproperty\tvalue";puts $nf "cell\tpin\tdirection\tnet"
    set count 0
    array set properties {}
    set started [clock milliseconds]
    foreach cell $raw {
        set name [get_property NAME $cell];set parent $name;set internal 0
        while {[set split [string last / $parent]] >= 0} {
            set parent [string range $parent 0 [expr {$split-1}]]
            if {[info exists names($parent)]} {set internal 1;break}
        }
        if {$internal} {continue}
        set kind [get_property REF_NAME $cell]
        if {![dict exists $modelProps $kind]} {error "No model properties for $kind"}
        puts $cf "$name\t$kind"
        if {![info exists properties($kind)]} {set properties($kind) [list_property $cell]}
        set present $properties($kind)
        foreach prop [dict get $modelProps $kind] {
            if {$prop in $present} {
                set value [get_property $prop $cell]
                if {[regexp {[\t\r\n]} $value]} {error "Unexpected property delimiter"}
                puts $pf "$name\t$prop\t$value"
            }
        }
        foreach pin [get_pins -of_objects $cell] {
            set net [get_nets -quiet -top_net_of_hierarchical_group -of_objects $pin]
            if {[llength $net] > 1} {error "Multiple top nets for $pin"}
            puts $nf "$name\t[get_property REF_PIN_NAME $pin]\t[get_property DIRECTION $pin]\t$net"
        }
        incr count
        if {$count % 1000 == 0 || $count == 100} {puts "PROTECTED_CELLS $count ELAPSED_MS [expr {[clock milliseconds]-$started}]";flush stdout}
    }
    close $cf;close $pf;close $nf
    set f [open [file join $output completion.tsv] w]
    puts $f "canonical_cells\t$count"
    puts $f "input_sha256\t[lindex [exec sha256sum -- $input] 0]"
    puts $f "vivado\t[version -short]";close $f
    puts "PROTECTED_PRIMITIVES_EXPORTED"
} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1}
exit 0
