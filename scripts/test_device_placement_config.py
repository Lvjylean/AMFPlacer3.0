import json
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from device_placement_config import validate_device_placement_binary
from paper_boundary_config import validate_paper_boundary_binary

class DevicePolicyConfigTest(unittest.TestCase):
    @patch("device_placement_config.subprocess.run")
    def test_old_configuration_compatible(self, run):
        for device in ("VCU108","U250"):
            validate_device_placement_binary({"device":device},"old")
        run.assert_not_called()

    @patch("device_placement_config.subprocess.run")
    def test_stretch_guard_on_legacy_path(self, run):
        cfg={"device":"U250","BoundaryAwareClustering":"false","ClockRegionColumnStretch":"false"}
        run.return_value=SimpleNamespace(returncode=0,stdout="{}")
        with self.assertRaisesRegex(ValueError,"refusing silent fallback"):
            validate_paper_boundary_binary(cfg,"old")
        run.return_value=SimpleNamespace(returncode=0,stdout=json.dumps(
            dict(device_placement_policy_schema=1,clock_region_column_stretch_schema=1)))
        validate_paper_boundary_binary(cfg,"new")

    @patch("device_placement_config.subprocess.run")
    def test_alias_needs_supported_binary(self, run):
        run.return_value=SimpleNamespace(returncode=0,stdout="{}")
        with self.assertRaisesRegex(ValueError,"device placement policy"):
            validate_device_placement_binary({"device":"VU095"},"old")

    @patch("device_placement_config.subprocess.run")
    def test_reserved_and_invalid(self, run):
        with self.assertRaisesRegex(ValueError,"not implemented"):
            validate_device_placement_binary({"device":"VHK158"},"new")
        with self.assertRaisesRegex(ValueError,"true or false"):
            validate_device_placement_binary({"ClockRegionColumnStretch":"yes"},"new")
        run.assert_not_called()
if __name__=="__main__": unittest.main()
