import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from diagnostics.retry_getrf_routing import validate_source, route_script


class RouteRetry(unittest.TestCase):
    def fixture(self, root):
        (root / 'reports').mkdir()
        values = {
            'manifest.json': {'stages': [{'name': 'amf', 'exit_code': 0}]},
            'reports/amf_coverage.json': {'input_cells': 2, 'assigned_cells': 2, 'missing_by_type': {}},
            'reports/placed_placement.json': {'requested': 2, 'present': 2, 'placed': 2},
            'reports/placed_cascades.json': {'checked': 1, 'violations': 0},
            'reports/placed_srl_cascades.json': {'checked': 1, 'violations': 0},
            'status.json': {'state': 'running', 'stage': 'vivado'},
        }
        for path, value in values.items():
            (root / path).write_text(json.dumps(value))
        (root / 'reports/getrf_placed.dcp').write_bytes(b'checkpoint fixture')

    def test_audited_placement_can_be_reused_while_original_router_runs(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.fixture(root)
            _, checkpoint = validate_source(root)
            self.assertEqual(checkpoint, root / 'reports/getrf_placed.dcp')

    def test_unvalidated_or_incomplete_source_is_rejected(self):
        bad = {
            'manifest.json': {'stages': [{'name': 'amf', 'exit_code': 1}]},
            'reports/amf_coverage.json': {'input_cells': 2, 'assigned_cells': 1, 'missing_by_type': {'MUXF7': 1}},
            'reports/placed_placement.json': {'requested': 2, 'present': 2, 'placed': 1},
            'reports/placed_cascades.json': {'checked': 1, 'violations': 1},
            'reports/placed_srl_cascades.json': {'checked': 1, 'violations': 1},
            'reports/getrf_placed.dcp': None,
        }
        for path, value in bad.items():
            with self.subTest(path=path), tempfile.TemporaryDirectory() as work:
                root = Path(work)
                self.fixture(root)
                if value is None:
                    (root / path).unlink()
                else:
                    (root / path).write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    validate_source(root)

    def test_retry_keeps_audits_reports_and_timing_constraints_without_replacement(self):
        script = route_script((ROOT / 'scripts/full_backend.tcl').read_text())
        self.assertNotIn('import_placement.tcl', script)
        self.assertNotIn('place_design', script)
        self.assertNotIn('getrf_placed.dcp', script)
        self.assertIn('audit placed', script)
        self.assertIn('audit routed', script)
        self.assertIn('route_design -directive AlternateCLBRouting', script)
        self.assertIn('report_timing_summary', script)
        self.assertIn('report_drc', script)
        self.assertIn('getrf_routed.dcp', script)
        self.assertNotIn('reset_timing', script)
        self.assertNotIn('create_clock', script)


if __name__ == '__main__':
    unittest.main()
