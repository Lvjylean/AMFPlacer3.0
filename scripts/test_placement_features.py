import itertools, json, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
from placement_features import CHOICES, validate, resolve, route_enabled
from export_placement_config import resolve as export, KEYS
from e2_incremental import publish

class Features(unittest.TestCase):
    def test_dependencies(self):
        for q,p,t,r in itertools.product(('false','true'),repeat=4):
            c=dict(SllQuantityEnable=q,SllPlanningEnable=p,SllTimingCorrectionEnable=t,SllRouteConstraintEnable=r)
            if p=='false' and (t=='true' or r=='true'):
                with self.assertRaises(ValueError):validate(c)
            else:
                validate(c);self.assertEqual(route_enabled(c),p=='true' and r=='true')
    def test_children_dormant(self):
        c,ignored=resolve(dict(SllQuantityEnable='false',SllDynamicTauEnable='true',SllPlanningEnable='false',SllCapacityOptimizationEnable='true'))
        self.assertEqual(c['SllDynamicTauEnable'],'false');self.assertEqual(c['SllCapacityOptimizationEnable'],'false')
        self.assertIn('SllDynamicTauEnable',ignored)
    def test_invalid_values(self):
        for key in CHOICES:
            with self.assertRaises(ValueError):validate({key:'typo'})
    def test_route_off_ignores_old_plan(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'placement').mkdir();(p/'placement/sll-plan.tcl').write_text('old plan')
            publish(dict(SllPlanningEnable='true',SllRouteConstraintEnable='false'),p)
            self.assertFalse((p/'sll-plan.tcl').exists())
    def test_legacy_route_default(self):
        self.assertTrue(route_enabled({'SllPlanningEnable':'true'}))
        self.assertFalse(route_enabled({}))
    def test_new_binary_required_without_planning(self):
        from sll_planning_inputs import validate as inputs
        with patch('sll_planning_inputs.subprocess.check_output',return_value='{}'):
            with self.assertRaisesRegex(ValueError,'decoupled'):
                inputs({'SllPlanningEnable':'false','TimingDrivenPlacementEnable':'false'},'old-binary')
    def test_integrated_export(self):
        root=Path(__file__).resolve().parents[1]
        c=json.loads((root/'configs/placement/placer-integrated.json').read_text())
        base={'device':'U250','ClockPeriod':'10','jobs':'12','vivado extracted device information file':'device.zip',
              'vivado extracted design information file':'design.zip','physical boundary model file':'physical.json'}
        c['IncrementalCompilation']='false';effective,report=export(c,base,'integrated')
        self.assertEqual(effective['SllJointPlacementEnable'],'true')
        self.assertEqual(effective['SllColumnCapacityEnable'],'true')
        self.assertEqual(effective['SllRegionPolicy'],'preserve')
        self.assertEqual(effective['SllPlanningAllowSplit'],'false')
        # Old profiles explicitly reject new keys, so they cannot silently ignore switches.
        with self.assertRaises(ValueError):export(c,base,'83')
    def test_backend_is_explicit(self):
        text=(Path(__file__).parent/'full_backend.tcl').read_text()
        self.assertNotIn('if {[file exists [file join $placement sll-plan.tcl]]}',text)
        self.assertEqual(text.count('if {[info exists ::env(AMF_SLL_ROUTE_CONSTRAINTS)]'),2)

if __name__=='__main__':unittest.main()
