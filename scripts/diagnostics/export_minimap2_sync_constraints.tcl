if {$argc != 2} {error "Expected checkpoint and new output directory"}
lassign $argv input output
if {[file exists $output]} {error "Refusing to overwrite $output"}
file mkdir $output
set_param general.maxThreads 4
open_checkpoint $input
set cells [get_cells -hier -filter {ASYNC_REG == TRUE}]
set f [open [file join $output async_regs.tsv] w]
puts $f "cell\tprimitive\tasync_reg\tdont_touch\tloc\tbel\tdriver"
foreach cell $cells {
    set dpin [get_pins -quiet -of_objects $cell -filter {REF_PIN_NAME == D}]
    set net [get_nets -quiet -top_net_of_hierarchical_group -of_objects $dpin]
    set drivers [get_pins -quiet -leaf -of_objects $net -filter {DIRECTION == OUT}]
    puts $f [join [list $cell [get_property REF_NAME $cell] [get_property ASYNC_REG $cell] [get_property DONT_TOUCH $cell] [get_property LOC $cell] [get_property BEL $cell] [join $drivers ,]] "\t"]
}
close $f
puts "ASYNC_REGISTER_COUNT [llength $cells]"
exit 0
