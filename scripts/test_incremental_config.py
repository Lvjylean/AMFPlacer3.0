"""Canonical/legacy jobs, opt-in guards, and executable compatibility."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from incremental_config import incremental_enabled, normalize_incremental_config, validate_incremental_binary
from run_dfx_multi import validate, load
from run_dfx import load_job
from create_dfx_shell import run as create_shell

class IncrementalConfigTests(unittest.TestCase):
    def job(self):
        return dict(clock_period_ns=12, partitions=[
            dict(id=name, active_partition="top/"+name, pblock="rp_"+name,
                 pblock_ranges=["CLOCKREGION_X1Y2:CLOCKREGION_X3Y2"])
            for name in ("a", "b")])

    def test_job_loaders_and_canonical_forwarding(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"job.json"
            outputs = []
            for key in ("IncrementalCompilation", "IncremenralCompilation"):
                for value in (True, "true"):
                    job = self.job(); job[key] = value
                    path.write_text(json.dumps(job))
                    actual = load(path)
                    self.assertTrue(incremental_enabled(actual))
                    self.assertNotIn("IncremenralCompilation", actual)
                    outputs.append(actual)
            self.assertEqual(outputs[0], outputs[2])
            self.assertEqual(outputs[1], outputs[3])

    def test_required_opt_in(self):
        for extra in ({}, {"IncrementalCompilation": False}, {"IncremenralCompilation": "false"}):
            with self.assertRaisesRegex(ValueError, "Explicit IncrementalCompilation"):
                validate(dict(self.job(), **extra))

    def test_conflicts_rejected_at_public_boundaries(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"job.json"
            for a, b in ((True, "false"), (False, "true")):
                job = dict(self.job(), IncrementalCompilation=a, IncremenralCompilation=b)
                path.write_text(json.dumps(job))
                for fn in (lambda: validate(copy.deepcopy(job)), lambda: load_job(path),
                           lambda: create_shell(SimpleNamespace(config=path))):
                    with self.assertRaisesRegex(ValueError, "Conflicting"):
                        fn()

    def test_equivalent_dual_keys_and_invalid_values(self):
        for a, b in ((True, "true"), (False, "false")):
            cfg = normalize_incremental_config(dict(IncrementalCompilation=a, IncremenralCompilation=b))
            self.assertEqual(cfg, {"IncrementalCompilation": a})
        for key in ("IncrementalCompilation", "IncremenralCompilation"):
            for value in (1, 0, None, "yes", ""):
                with self.assertRaises(ValueError):
                    normalize_incremental_config({key: value})

    def test_old_binary_rejected_before_execution(self):
        for cap in ({}, {"dfx_incremental_schema": 1}):
            with patch("incremental_config.subprocess.run",
                       return_value=SimpleNamespace(returncode=0, stdout=json.dumps(cap))):
                with self.assertRaisesRegex(ValueError, "refusing silent fallback"):
                    validate_incremental_binary({"IncrementalCompilation": True}, "old")
        with patch("incremental_config.subprocess.run",
                   return_value=SimpleNamespace(returncode=0, stdout='{"incremental_compilation_key_schema":1}')):
            for key in ("IncrementalCompilation", "IncremenralCompilation"):
                cfg = {key: True}; validate_incremental_binary(cfg, "new")
                self.assertEqual(cfg, {"IncrementalCompilation": True})

    def test_ordinary_mode_does_not_require_new_binary(self):
        with patch("incremental_config.subprocess.run") as called:
            for cfg in ({}, {"IncrementalCompilation": False}, {"IncremenralCompilation": "false"}):
                validate_incremental_binary(cfg, "ordinary")
            called.assert_not_called()

if __name__ == "__main__":
    unittest.main()
