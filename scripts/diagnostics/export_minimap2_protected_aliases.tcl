# Read actual hierarchical net segments, including protected module boundaries.
if {$argc != 3} {error "Expected DCP, existing pins TSV, new output directory"}
lassign $argv input pinsfile output
if {[file exists $output]} {error "Output exists"}
file mkdir $output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open $pinsfile r];gets $f header;set names {}
    while {[gets $f line] >= 0} {
        set n [lindex [split $line "\t"] 3]
        if {$n ne ""} {dict set names $n 1}
    }
    close $f
    set f [open [file join $output aliases.tsv] w]
    puts $f "net\tsegment"
    set count 0;set pairs 0
    array set objects {}
    set all [get_nets -hier]
    foreach name [get_property NAME $all] object $all {
        if {[dict exists $names $name]} {set objects($name) $object}
    }
    unset all
    foreach n [dict keys $names] {
        if {![info exists objects($n)]} {error "Cannot resolve exact net $n"}
        set nets $objects($n)
        set anchor [lindex [get_pins -quiet -of_objects $nets] 0]
        if {$anchor eq ""} {error "No anchor pin for $n"}
        foreach alias [get_property NAME [get_nets -segments -boundary_type both -of_objects $anchor]] {
            if {$alias ne $n} {puts $f "$n\t$alias";incr pairs}
        }
        incr count
        if {$count % 5000 == 0} {puts "ALIAS_NETS $count PAIRS $pairs";flush stdout}
    }
    close $f
    set f [open [file join $output completion.tsv] w]
    puts $f "queried_nets\t$count";puts $f "alias_pairs\t$pairs"
    puts $f "input_sha256\t[lindex [exec sha256sum -- $input] 0]"
    puts $f "vivado\t[version -short]";close $f
    puts "PROTECTED_ALIASES_EXPORTED"
} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1}
exit 0
