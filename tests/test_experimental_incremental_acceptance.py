import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_parallel_acceptance import require_qor_acceptance

class ExperimentalAcceptanceTest(unittest.TestCase):
    def test_only_explicit_timing_failure_is_allowed(self):
        fail = dict(implementation_verified=True, timing_met=False)
        job = {'native_static_cache': {'baseline_quality_policy': 'allow-failed-for-experiment'}}
        with self.assertRaises(RuntimeError):require_qor_acceptance({}, fail)
        self.assertTrue(require_qor_acceptance(job, fail)['timing_failure_retained'])
        self.assertFalse(fail['timing_met'])
        with self.assertRaises(RuntimeError):require_qor_acceptance(job, dict(fail, implementation_verified=False))
        self.assertFalse(require_qor_acceptance({}, dict(fail, timing_met=True))['timing_failure_retained'])
        with self.assertRaises(ValueError):require_qor_acceptance({'native_static_cache': {'baseline_quality_policy': 'typo'}}, fail)

if __name__ == '__main__':unittest.main()
