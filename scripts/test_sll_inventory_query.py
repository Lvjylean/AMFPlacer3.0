#!/usr/bin/env python3
"""Bounded SLL inventory handles aliases without whole-design net expansion."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parent
class InventoryQuery(unittest.TestCase):
 def evaluate(self,body):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'case.tcl'
   prefix='source {'+str(ROOT/'sll_inventory_query.tcl')+'}\n'
   prefix+='''set calls 0
proc get_nodes {args} {
 global calls
 incr calls
 if {[lsearch $args -of_objects]>=0} {error "Whole-design expansion forbidden"}
 set result {}
 foreach name [lindex $args end] {
  if {$name eq "missing"} {continue}
  if {$name eq "alias"} {set name canonical}
  if {$name ni $result} {lappend result $name}
 }
 return $result
}
proc get_property {property objects} {
 set result {}
 foreach object $objects {
  if {$property eq "NAME"} {lappend result $object} else {lappend result [expr {$object in {used canonical}}]}
 }
 return $result
}
'''
   p.write_text(prefix+body)
   return subprocess.run(['tclsh',str(p)],capture_output=True,text=True)
 def test_batched_used_and_unused(self):
  result=self.evaluate('set used [e2_sll_inventory::used_candidates {used 1 unused 1 third 1} 2]\nif {$used ne {used 1} || $calls!=2} {error "Wrong bounded result"}\n')
  self.assertEqual(result.returncode,0,result.stderr)
 def test_alias_is_marked_using_requested_name(self):
  result=self.evaluate('set used [e2_sll_inventory::used_candidates {alias 1 unused 1}]\nif {$used ne {alias 1}} {error "Alias occupancy lost"}\n')
  self.assertEqual(result.returncode,0,result.stderr)
 def test_empty_candidates(self):
  result=self.evaluate('set used [e2_sll_inventory::used_candidates {}]\nif {$used ne {} || $calls!=0} {error "Empty inventory queried device"}\n')
  self.assertEqual(result.returncode,0,result.stderr)
 def test_missing_candidate_fails_closed(self):
  self.assertNotEqual(self.evaluate('e2_sll_inventory::used_candidates {missing 1 used 1}\n').returncode,0)
 def test_whole_resource_must_fit_one_footprint(self):
  result=self.evaluate('set got [e2_sll_inventory::eligible_candidates {r {A/x B/y}} {one {A 1} two {B 1}}]\nif {$got ne {}} {error "Combined unrelated footprints"}\n')
  self.assertEqual(result.returncode,0,result.stderr)
 def test_both_directed_views_are_checked(self):
  result=self.evaluate('set got [e2_sll_inventory::eligible_candidates {r {A/x B/y A/z}} {one {A 1 B 1}}]\nif {[lsort [dict keys $got]] ne {A/x A/z B/y}} {error "Lost directed view"}\n')
  self.assertEqual(result.returncode,0,result.stderr)
if __name__=='__main__':unittest.main()
