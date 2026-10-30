"""Regression for the observed missing-support-file snapshot failure."""
import ast
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SnapshotTests(unittest.TestCase):
    def test_actual_runner_snapshot_includes_support_file(self):
        source = ast.parse((ROOT / "scripts/run_dfx_full.py").read_text())
        loops = [n for n in ast.walk(source) if isinstance(n, ast.For)
                 and ast.unparse(n.iter) == "script.iterdir()"]
        self.assertEqual(len(loops), 1)
        code = compile(ast.fix_missing_locations(ast.Module(body=loops, type_ignores=[])),
                       "actual_run_dfx_full_snapshot", "exec")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script, out = root / "scripts", root / "run"
            script.mkdir()
            (out / "inputs").mkdir(parents=True)
            expected = ["runner.py", "backend.tcl", "topology-guard-nets.txt"]
            for name in expected + ["unrelated.txt", "run.log"]:
                (script / name).write_text(name + "\n")
            (script / "directory.py").mkdir()
            exec(code, {"script": script, "out": out, "shutil": shutil})
            self.assertEqual(sorted(p.name for p in (out / "inputs").iterdir()),
                             sorted(expected))
            for name in expected:
                self.assertEqual((script / name).read_bytes(),
                                 (out / "inputs" / name).read_bytes())

    def test_default_backend_does_not_call_case_workarounds(self):
        backend = (ROOT / "scripts/dfx_full_backend.tcl").read_text()
        self.assertIn("{place_design}", backend)
        self.assertNotIn("full_topology_guard::", backend)
        self.assertNotIn("full_carry_preroute::", backend)
        self.assertIn("dfx_sll::apply_full", backend)


if __name__ == "__main__":
    unittest.main()
