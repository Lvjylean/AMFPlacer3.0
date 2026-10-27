# Run in an empty, linked design. No existing design is changed.
if {![info exists e2_sll_export_directory]} {error "Set e2_sll_export_directory before sourcing this script in an empty linked design"}
set out [file normalize $e2_sll_export_directory]
if {[file exists $out]} {error "Refusing to overwrite SLL inventory $out"}
file mkdir $out
set f [open $out/channels-raw.tsv w]
puts $f "resource\tfrom\tto\tcolumn\tin_tile\tout_tile\tsegment\tnode_models\tpip_models"
set models [open $out/speed-models.tsv w]
puts $models "name\tslow_max\tslow_min\tfast_max\tfast_min"
array set exportedModel {}
proc record_models {objects} {
 global models exportedModel
 set names [get_speed_models -quiet -of_objects $objects]
 foreach m $names {
  if {![info exists exportedModel($m)]} {
   set exportedModel($m) 1
   puts $models [join [list $m [get_property SLOW_MAX $m] [get_property SLOW_MIN $m] [get_property FAST_MAX $m] [get_property FAST_MIN $m]] "\t"]
  }
 }
 return $names
}
set count 0
set rejected 0
set nodes [lsort -unique [get_property NAME [get_nodes -of_objects [get_tiles -filter {TYPE == LAG_LAG}] -filter {IS_SLL == 1 && IS_CROSSING_SLRS == 1}]]]
foreach name $nodes {
 set b [get_nodes -of_objects [get_tiles [lindex [split $name /] 0]] -filter "NAME == $name"]
 set endpoints [get_nodes -uphill -of_objects $b]
 foreach tx $endpoints {
  if {![string match */LAG_MUX_ATOM_*_TXOUT $tx]} {continue}
  set from [get_slrs -of_objects [get_tiles -of_objects $tx]]
  if {[llength $from]!=1} {incr rejected;continue}
  set in {}
  foreach p [get_nodes -uphill -of_objects $tx] {if {[string match INT_*/IMUX_* $p]} {lappend in $p}}
  if {[llength $in]!=1} {incr rejected;continue}
  set rx {}
  foreach p [get_nodes -downhill -of_objects $b] {
   if {[string match */LAG_LAGUNA_SITE_*_RXD* $p] && [get_slrs -of_objects [get_tiles -of_objects $p]] ne $from} {lappend rx $p}
  }
  if {[llength $rx]!=1} {incr rejected;continue}
  set to [get_slrs -of_objects [get_tiles -of_objects $rx]]
  set exit {}
  foreach p [get_nodes -downhill -of_objects $rx] {if {[regexp {^LAG_LAG_.*\/RXD[0-9]+$} $p]} {lappend exit $p}}
  if {[llength $exit]!=1 || [llength $to]!=1} {incr rejected;continue}
  set seg [list $in $tx $b $rx $exit]
  set nm {};set pm {}
  foreach p $seg {lappend nm [join [record_models $p] ,]}
  foreach a [lrange $seg 0 end-1] z [lrange $seg 1 end] {
   set found {}
   foreach p [get_pips -downhill -of_objects $a] {
    if {[get_property NAME [get_nodes -downhill -of_objects $p]] eq $z} {lappend found $p}
   }
   if {[llength $found]!=1} {error "Ambiguous segment pip $a -> $z: $found"}
   lappend pm [join [record_models $found] ,]
  }
  puts $f [join [list $name [string range $from 3 end] [string range $to 3 end] [get_property COLUMN [get_tiles [lindex [split $name /] 0]]] [lindex [split $in /] 0] [lindex [split $exit /] 0] [join $seg ,] [join $nm |] [join $pm |]] "\t"]
  incr count
  if {$count%2000==0} {flush $f;flush $models;puts "E2_SLL_EXPORT $count"}
 }
}
close $f;close $models
set f [open $out/metadata.tsv w]
puts $f "part\t[get_property PART [current_design]]\nvivado\t[version -short]\nchannels\t$count\nrejected\t$rejected\ndelay_units\tns\nsegment\tIMUX_to_RXD_egress\navailability\tempty_device"
close $f
puts "E2_SLL_EXPORT_DONE $count $rejected"
