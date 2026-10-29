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
class TclScope(unittest.TestCase):
 def run_case(self,body):
  import subprocess
  helper=Path(__file__).resolve().parent/'dfx_sll.tcl'
  with tempfile.TemporaryDirectory() as t:
   script=Path(t)/'check.tcl'
   script.write_text('source {'+str(helper)+'}\n'+
       'proc get_nodes {args} {return [lindex $args end]}\n'+
       'proc get_nets {args} {global occupied;return $occupied}\n'+
       'set occupied {}\nset used {}\nset fp {T 1 U 1}\n'+body)
   return subprocess.run(['tclsh',str(script)],capture_output=True,text=True)
 def test_shared_tree_and_local_receiver(self):
  body='set tree {{pin rm/local} {segment {T/A U/B} children {{pin rm/a} {pin rm/b}}}}\ndfx_sll::validate_tree $tree rm/ $fp used\n'
  self.assertEqual(self.run_case(body).returncode,0)
 def test_scope_footprint_occupancy_and_collisions_rejected(self):
  cases=[
   'set tree {{pin other/a}}',
   'set tree {{segment {T/A Z/B} children {{pin rm/a}}}}',
   'set occupied static_net;set tree {{segment {T/A U/B} children {{pin rm/a}}}}',
   'dict set used T/A 1;set tree {{segment {T/A U/B} children {{pin rm/a}}}}']
  for case in cases:
   result=self.run_case(case+'\ndfx_sll::validate_tree $tree rm/ $fp used\n')
   self.assertNotEqual(result.returncode,0)
 def test_disabled_has_no_design_queries(self):
  self.assertEqual(self.run_case('set dfx(e2_sll_enabled) 0\ndfx_sll::apply\n').returncode,0)
class TclConnectivity(unittest.TestCase):
 def test_actual_boundary_net_is_rejected(self):
  import subprocess
  helper=Path(__file__).resolve().parent/'dfx_sll.tcl'
  for pins,ports,expected in [('rm/source/Q rm/sink/D','',0),('rm/source/Q static/D','',1),('rm/source/Q','top_port',1)]:
   body='source {'+str(helper)+'}\n'
   body+='proc get_pins {args} {if {[lsearch $args -leaf]>=0} {return {'+pins+'}};return rm/source/Q}\n'
   body+='proc get_nets {args} {return rm/s}\n'
   body+='proc get_ports {args} {return {'+ports+'}}\n'
   body+='proc get_property {p objects} {return $objects}\n'
   body+='dfx_sll::validate_internal_net rm/source/Q rm/\n'
   with tempfile.TemporaryDirectory() as t:
    f=Path(t)/'case.tcl';f.write_text(body)
    result=subprocess.run(['tclsh',str(f)],capture_output=True,text=True)
   self.assertEqual(bool(result.returncode),bool(expected),result.stderr)
class DiagnosticOutputs(unittest.TestCase):
 def test_neutral_template_gets_private_outputs(self):
  from e2_incremental import configure_diagnostics
  with tempfile.TemporaryDirectory() as temp:
   a={'SllQuantityEnable':'true'};b=dict(a)
   configure_diagnostics(a,Path(temp)/'a');configure_diagnostics(b,Path(temp)/'b')
   self.assertNotEqual(a['SllReportDirectory'],b['SllReportDirectory'])
   self.assertFalse(Path(a['SllTraceNetList']).exists())
   self.assertEqual(a['SllQuantityEnable'],'true')
   configure_diagnostics(a,Path(temp)/'a')
 def test_explicit_trace_is_preserved_and_disabled_is_noop(self):
  from e2_incremental import configure_diagnostics
  with tempfile.TemporaryDirectory() as temp:
   t=Path(temp)/'trace';t.write_text('example/net\n')
   c={'SllQuantityDiagnostics':'true','SllTraceNetList':str(t)}
   configure_diagnostics(c,Path(temp)/'run')
   self.assertEqual(c['SllTraceNetList'],str(t));self.assertEqual(t.read_text(),'example/net\n')
   off={};configure_diagnostics(off,Path(temp)/'off');self.assertEqual(off,{})
if __name__=='__main__':unittest.main()
