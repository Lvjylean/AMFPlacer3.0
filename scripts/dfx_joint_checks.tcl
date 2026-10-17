source [file join [file dirname [info script]] dfx_multi_common.tcl]
source [file join [file dirname [info script]] dfx_boundary_compatibility.tcl]
source [file join [file dirname [info script]] dfx_clock_contract.tcl]
namespace eval joint {
 proc worker {id} {global dfx;return [set dfx(worker_$id)]}
 proc requested {id rp} {
  set f [open [file join [worker $id] placement.tsv] r];set data {}
  while {[gets $f line]>=0} {
   if {$line eq ""} {continue}
   set fields [split $line \t]
   if {[llength $fields]!=2} {error "Malformed AMF placement row"}
   lassign $fields c target
   if {[string first "$rp/" $c]!=0 || [dict exists $data $c]} {error "Wrong-scope/duplicate placement: $c"}
   dict set data $c $target
  }
  close $f
  global dfx
  if {[dict size $data] != [set dfx(expected_cells_$id)]} {error "Incomplete AMF cell inventory in $id"}
  return $data
 }
 proc verify_placement {id rp pb label requested} {
  global dfx
  # RP membership alone includes prohibited boundary sites. Check live legality
  # as well as the cached static occupancy before routing or accepting a DCP.
  set allowed {};foreach s [get_sites -of_objects [get_pblocks $pb] -filter {PROHIBIT == 0}] {dict set allowed $s 1}
  set blocked {};set f [open [file join [worker $id] blocked_sites.txt] r]
  foreach s [split [string trim [read $f]] \n] {if {$s ne ""} {dict set blocked $s 1}};close $f
  set before [current_instance];current_instance $rp
  set code [catch {set cells [get_cells -quiet -hierarchical -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}]} value opts]
  current_instance
  if {$before ne ""} {current_instance $before}
  if {$code} {return -options $opts $value}
  set seen {};set f [open $dfx(output)/placement_retention.tsv a]
  foreach c $cells {
   if {[amf_dfx::constant $c]} {continue}
   set name [get_property NAME $c]
   if {![dict exists $requested $name]} {error "Unexported dynamic primitive at $label: $name"}
   if {[dict exists $seen $name]} {error "Duplicate dynamic primitive"}
   dict set seen $name 1
   set target [dict get $requested $name]
   set loc [get_property LOC $c];set bel [get_property BEL $c]
   set actual $loc
   if {[string first / $target]>=0} {set actual "$loc/[lindex [split $bel .] end]"}
   puts $f [join [list $label $id $name $target $loc $bel] \t]
   if {$actual ne $target} {error "AMF LOC/BEL changed at $label: $name requested=$target actual=$actual"}
   if {![dict exists $allowed $loc] || [dict exists $blocked $loc]} {error "Dynamic placement outside allowed region: $name"}
  }
  close $f
  if {[lsort [dict keys $seen]] ne [lsort [dict keys $requested]]} {error "AMF cell coverage changed at $label/$id"}
  set f [open $dfx(output)/placement_retention_counts.tsv a]
  puts $f "$label\t$id\t[dict size $requested]\t[dict size $seen]\t0";close $f
  puts "AMF_JOINT_PLACEMENT_EXACT stage=$label id=$id cells=[dict size $seen]"
 }
 proc audit {label requests boundaries footprints} {
  global dfx
  dfx_clock_contract::assert_top $label 1
  amf_multi::validate_regions
  amf_dfx::check_static_placement $dfx(output)/static_snapshot.tcldata $label multi
  foreach row $dfx(rps) {
   lassign $row id rp pb
   set dfx(active_partition) $rp;set dfx(pblock) $pb
   amf_dfx::assert_context
   verify_placement $id $rp $pb $label [dict get $requests $id]
   if {[amf_multi::route_footprint $rp] ne [dict get $footprints $id]} {error "Joint RP footprint changed at $label/$id"}
   amf_dfx::save_dict $dfx(output)/[set id]_[set label]_boundary.tcldata [dfx_boundary::compare [dict get $boundaries $id] [amf_dfx::boundary_snapshot]]
  }
  amf_multi::reports $dfx(output)/$label
  dfx_clock_contract::assert_coverage $dfx(output)/${label}_timing.rpt $label
 }
}
