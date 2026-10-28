import json, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
from e2_incremental import allocate, bind, publish, copy_plan, digest, prepare

def row(resource,segment):return dict(resource=resource,segment=segment)
class Leases(unittest.TestCase):
 def test_reverse_directions_and_shared_entrances_are_indivisible(self):
  rows=[row('r1','T/A,U/B'),row('r1','U/C,T/D'),row('r2','T/A,U/E'),row('r3','T/F,U/G')]
  reserved,s=allocate(rows,set(),{'a':{'T','U'},'b':{'T','U'}})
  self.assertEqual(('r1' in reserved['a']),('r2' in reserved['a']))
  self.assertEqual(reserved['a']&reserved['b'],set())
  self.assertEqual(reserved['a']|reserved['b'],{'r1','r2','r3'})
  self.assertEqual(s['physical_resources'],3)
 def test_occupied_entrance_blocks_entire_component(self):
  rows=[row('r1','T/A,U/B'),row('r2','T/A,U/E')]
  res,_=allocate(rows,{'U/B'},{'a':{'T','U'}})
  self.assertEqual(res['a'],{'r1','r2'})
 def test_both_sides_must_be_inside_footprint(self):
  res,_=allocate([row('r','T/A,U/B')],set(),{'a':{'T'},'b':{'T','U'}})
  self.assertEqual(res['a'],{'r'});self.assertFalse(res['b'])
 def test_order_independent(self):
  rows=[row('r1','T/A,U/B'),row('r2','T/C,U/D')]
  self.assertEqual(allocate(rows,set(),{'a':{'T','U'},'b':{'T','U'}}),
                   allocate(rows[::-1],set(),{'b':{'T','U'},'a':{'T','U'}}))
 def test_disabled_requires_no_contract(self):
  cfg={};self.assertEqual(bind({},cfg),{})
 def test_missing_reservation_rejected(self):
  with self.assertRaises(ValueError):bind({},dict(SllPlanningEnable='true'))
 def test_bound_and_hash_verified(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'r';p.write_text('r1\n')
   job={'e2_sll_reservation':{'path':str(p),'sha256':digest(p)}}
   cfg=bind(job,dict(SllPlanningEnable='true'))
   self.assertEqual(cfg['SllIncrementalScope'],'internal-active-nets')
   p.write_text('changed')
   with self.assertRaises(ValueError):bind(job,cfg)
 def test_published_plan_must_exist_and_keep_hash(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'placement').mkdir();(p/'dest').mkdir();r=p/'r';r.write_text('')
   cfg=dict(SllPlanningEnable='true',SllReservedResourceFile=str(r))
   with self.assertRaises(ValueError):publish(cfg,p)
   (p/'placement/sll-plan.tcl').write_text('set e2_sll_plan {}\n');publish(cfg,p)
   copy_plan(p,p/'dest');self.assertEqual(digest(p/'sll-plan.tcl'),digest(p/'dest/sll-plan.tcl'))
   (p/'sll-plan.tcl').write_text('changed')
   with self.assertRaises(ValueError):copy_plan(p,p/'dest')
if __name__=='__main__':unittest.main()
