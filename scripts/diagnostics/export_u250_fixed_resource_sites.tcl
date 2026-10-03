# Export complete I/O and BUFG site columns, preserving uniform clock regions.
if {$argc != 2} {error "Expected input.dcp new output.tsv"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set f [open $output w]
    puts $f "site\ttile\tclock_region\tsite_type\ttile_type\trpm_x\trpm_y\tslr\tprohibited\tbels"
    set sites [get_sites -filter {SITE_TYPE == BUFGCE || SITE_TYPE == HPIOB_M || SITE_TYPE == HPIOB_S || SITE_TYPE == HPIOB_SNGL}]
    foreach site_name [lsort $sites] {
        set site [get_sites $site_name]
        set tile [get_tiles -of_objects $site]
        set slr [get_slrs -of_objects $site]
        if {![regexp {^SLR([0-9]+)$} $slr -> slr_id]} {error "Invalid SLR for $site"}
        puts $f [join [list $site $tile [get_property CLOCK_REGION $site] [get_property SITE_TYPE $site] [get_property TYPE $tile] [get_property RPM_X $site] [get_property RPM_Y $site] $slr_id [expr {[get_property PROHIBIT $site]?1:0}] [join [get_bels -of_objects $site] ,]] "\t"]
    }
    close $f
    puts "ISPD_FIXED_RESOURCE_SITES [llength $sites]"
} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorinfo]
    exit 1
}
exit 0
