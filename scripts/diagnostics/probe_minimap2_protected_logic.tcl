# Read-only, ordinary Vivado property access. Respect unavailable properties.
if {$argc != 2} {error "Expected DCP and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite"}
file mkdir $output
set_param general.maxThreads 4
if {[catch {
    open_checkpoint $input
    set selected [get_cells -hier -quiet -filter {NAME =~ *pcie_c2h_rst_3ff_reg* || NAME =~ *pcie_dsc_rst_3ff_reg*}]
    set luts [get_cells -hier -quiet -filter {NAME =~ *udma_wrapper* && REF_NAME =~ LUT*}]
    set f [open [file join $output properties.tsv] w]
    puts $f "cell\tproperty\tvalue"
    foreach cell [concat $selected [lrange $luts 0 2]] {
        set props [list_property $cell]
        foreach p {REF_NAME INIT IS_C_INVERTED IS_D_INVERTED IS_PRE_INVERTED IS_ENCRYPTED IS_BLACKBOX} {
            if {$p in $props} {
                if {[catch {get_property $p $cell} value]} {set value "UNAVAILABLE: $value"}
                puts $f "$cell\t$p\t$value"
            }
        }
    }
    close $f
    puts "PROTECTED_PROPERTY_PROBE_COMPLETED"
} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1}
exit 0
