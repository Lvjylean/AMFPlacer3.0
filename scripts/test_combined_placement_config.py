"""Combined-source gates; fixture-only, no remote commands or placement."""
import json
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from export_placement_config import resolve
from device_placement_config import validate_device_placement_binary

class CombinedConfigTests(unittest.TestCase):
    def setUp(self):
        self.cfg=dict(device="U250",ClockPeriod="10",jobs="12",visualization="true",
          ProFFClustering="true",ProPrimitiveTiming="false",ProLegacyIOBoundary="false",
          BoundaryAwareClustering="true",noBlockAware="false",
          BoundaryClusteringStrategy="cr-slr-hierarchical",BoundaryStageMode="independent-max2",
          BoundaryRegionClipping="true",BoundaryWeakAttraction="true",
          BoundaryAnchorStopOnRegionEntry="false",ClockRegionColumnStretch="false",
          IncrementalCompilation="false")
        self.base={"device":"U250","ClockPeriod":"10","jobs":"12",
          "vivado extracted device information file":"device.zip",
          "vivado extracted design information file":"netlist.zip",
          "physical boundary model file":"boundary.json"}
    def test_all_hierarchical_modes_with_visualization(self):
        for strategy in ("slr-hierarchical","cr-slr-hierarchical"):
            for mode in ("independent-max2","merged"):
                cfg=dict(self.cfg,BoundaryClusteringStrategy=strategy,BoundaryStageMode=mode)
                out,report=resolve(cfg,self.base,"combined")
                self.assertEqual(out["visualization"],"true")
                self.assertEqual(out["BoundaryStageMode"],mode)
                self.assertEqual(out["PhysicalBoundaryMode"],"true")
                self.assertNotIn("ClockRegionColumnStretch",out)
    def test_vu095_restored_stretch_and_trace_are_orthogonal(self):
        cfg=dict(self.cfg,device="VU095",BoundaryAwareClustering="false",ClockRegionColumnStretch="true")
        out,_=resolve(cfg,dict(self.base,device="VCU108"),"combined")
        self.assertEqual(out["ClockRegionColumnStretch"],"true")
        self.assertEqual(out["visualization"],"true")
    def test_disabled_trace_clears_legacy_keys(self):
        out,_=resolve(dict(self.cfg,visualization="false"),dict(self.base,DumpAllCoordTrace="old"),"combined")
        self.assertEqual(out["visualization"],"false")
        self.assertNotIn("DumpAllCoordTrace",out)
    def pilot(self):
        cfg=dict(self.cfg,device="VHK158",noBlockAware="true",BoundaryAwareClustering="false")
        base=dict(self.base,device="VHK158",VHK158LutFfPilot="true",PhysicalBoundaryMode="false")
        base["clock region geometry file"]="geometry.json"
        return cfg,base
    @patch("device_placement_config.subprocess.run")
    def test_guarded_vhk_needs_matching_binary(self,run):
        cfg,base=self.pilot();out,_=resolve(cfg,base,"combined")
        run.return_value=SimpleNamespace(returncode=0,stdout=json.dumps(dict(
          device_placement_policy_schema=1,clock_region_column_stretch_schema=1,vhk158_sparse_lutff_schema=1)))
        validate_device_placement_binary(out,"fixture")
        run.return_value=SimpleNamespace(returncode=0,stdout='{"device_placement_policy_schema":1}')
        with self.assertRaisesRegex(ValueError,"VHK158 sparse"):
            validate_device_placement_binary(out,"fixture")
    def test_vhk_unsupported_combinations(self):
        cfg,base=self.pilot()
        for key in ("BoundaryAwareClustering","ClockRegionColumnStretch","IncrementalCompilation",
                    "ProPrimitiveTiming","ProLegacyIOBoundary"):
            with self.assertRaises(ValueError):resolve(dict(cfg,**{key:"true"}),base,"combined")
        with self.assertRaises(ValueError):resolve(cfg,self.base,"combined")
    def test_no_implicit_dfX_context(self):
        with self.assertRaisesRegex(ValueError,"context is missing"):
            resolve(dict(self.cfg,IncrementalCompilation="true"),self.base,"combined")

if __name__=="__main__":unittest.main()
