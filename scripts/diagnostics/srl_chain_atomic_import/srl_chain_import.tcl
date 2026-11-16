namespace eval amf_srl_import {variable groups {};variable owners {};variable targets {};variable status {};variable messages {};variable report {}}
proc amf_srl_import::init {data reportPath} {
 variable groups;variable owners;variable targets;variable status;variable messages;variable report
 set groups {};set owners {};set targets {};set status {};set messages {};set report $reportPath
 set f [open $data r]
 try {
  gets $f header
  while {[gets $f line]>=0} {
   if {$line eq ""} {continue}
   set fields [split $line \t];if {[llength $fields]!=3} {error "Invalid SRL chain inventory"}
   lassign $fields id cell target
   if {[dict exists $owners $cell]} {error "Duplicate SRL member: $cell"}
   dict set owners $cell $id;dict set targets $cell $target;dict lappend groups $id $cell;dict lappend groups $id $target
  }
 } finally {close $f}
 set f [open $report w];puts $f "chain\tcells\trc\tmessage";close $f
}
proc amf_srl_import::place {pairs} {
 variable groups;variable owners;variable targets;variable status;variable messages;variable report
 if {[llength $pairs]%2} {error "Odd placement list"}
 set normal {};set ids {};set seen {}
 foreach {cell target} $pairs {
  if {[dict exists $owners $cell]} {
   if {$target ne [dict get $targets $cell]} {error "SRL target changed: $cell"}
   set id [dict get $owners $cell]
   if {![dict exists $seen $id]} {lappend ids $id;dict set seen $id 1}
  } else {lappend normal $cell $target}
 }
 set errors {}
 foreach id $ids {
  if {![dict exists $status $id]} {
   set whole [dict get $groups $id]
   set rc [catch {amf_carry_import::logged_place $whole} msg]
   dict set status $id $rc;dict set messages $id $msg
   set f [open $report a];puts $f "$id\t[expr {[llength $whole]/2}]\t$rc\t[string map [list \n { } \r { } \t { }] $msg]";close $f
   puts "AMF_SRL_CHAIN_IMPORT chain=$id cells=[expr {[llength $whole]/2}] rc=$rc"
  }
  if {[dict get $status $id]} {lappend errors [dict get $messages $id]}
 }
 if {[llength $normal] && [catch {amf_carry_import::place $normal} msg]} {lappend errors $msg}
 if {[llength $errors]} {return -code error [join $errors \n]}
 return {}
}
