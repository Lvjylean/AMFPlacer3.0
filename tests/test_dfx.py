import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('run_dfx', ROOT/'scripts/run_dfx.py')
dfx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dfx)


class DfxContractTests(unittest.TestCase):
    def test_tcl_settings_are_literal_data(self):
        value = 'file [error injected] $x {a};"\\name\nnext'
        script = 'set x '+dfx.tcl_word(value)+'\nputs -nonewline $x\n'
        got = subprocess.check_output(['tclsh'], input=script, text=True)
        self.assertEqual(value, got)

    def test_export_checks_exact_coverage_and_site_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'placement.tcl'
            path.write_text('error {DFX placement data: not executable}\n'
                            'set result [catch {place_cell {rm/a[0] SLICE_X1Y1/AFF}}]\n')
            self.assertEqual(dfx.placement_rows(path, {'rm/a[0]'}, {'SLICE_X1Y1'}, set()),
                             {'rm/a[0]':'SLICE_X1Y1/AFF'})
            for cells, allowed, blocked in [({'static/a'}, {'SLICE_X1Y1'}, set()),
                                            ({'rm/a[0]','rm/b'}, {'SLICE_X1Y1'}, set()),
                                            ({'rm/a[0]'}, {'SLICE_X2Y2'}, set()),
                                            ({'rm/a[0]'}, {'SLICE_X1Y1'}, {'SLICE_X1Y1'})]:
                with self.assertRaises(ValueError): dfx.placement_rows(path, cells, allowed, blocked)

    def test_duplicate_placement_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'placement.tcl'
            p.write_text('error {DFX placement data: x}\nset result [catch {place_cell {rm/a S/AFF rm/a S/BFF}}]\n')
            with self.assertRaises(ValueError): dfx.placement_rows(p, {'rm/a'}, {'S'}, set())

    def test_explicit_enable_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'job.json'
            for value in (False, 'false', None):
                p.write_text(json.dumps({'IncremenralCompilation':value}))
                with self.assertRaisesRegex(ValueError, 'explicit'): dfx.load_job(p)


if __name__ == '__main__': unittest.main()
