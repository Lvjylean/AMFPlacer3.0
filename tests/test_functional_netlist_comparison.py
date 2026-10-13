import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/diagnostics/compare_minimap2_functional_netlists.py'
spec = importlib.util.spec_from_file_location('functional_compare', SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class FunctionalComparison(unittest.TestCase):
    def fixture(self, path, init="4'h8", swapped=False, attribute='SLICE_X0Y0'):
        ports = '.I1(b), .I0(a), .O(y)' if swapped else '.I0(a), .I1(b), .O(y)'
        path.write_text('module top(a,b,y);\ninput a;\ninput b;\noutput y;\n'
                        f'(* LOC = "{attribute}" *)\nLUT2 #(.INIT({init})) \\core/lut [{ports}];\n'.replace('[', '(').replace(']', ')')
                        + 'endmodule\n`pragma protect begin_protected\nopaque\n`pragma protect end_protected\n')
        return audit.parse(path)

    def test_physical_attributes_and_named_association_order(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            a = self.fixture(p/'a.v')
            b = self.fixture(p/'b.v', swapped=True, attribute='SLICE_X9Y9')
            result = audit.compare(a, b, p/'diff.jsonl')
            self.assertTrue(result['identical_visible_structure'])
            self.assertFalse(result['formal_equivalence_proven'])
            self.assertEqual(a[2]['encrypted_blocks'], 1)

    def test_real_lut_function_change_is_detected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            result = audit.compare(self.fixture(p/'a.v'), self.fixture(p/'b.v', init="4'hE"), p/'diff.jsonl')
            self.assertEqual(result['counts']['parameters_changed'], 1)
            self.assertFalse(result['identical_visible_structure'])

    def test_unknown_behavioral_syntax_fails_closed(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'bad.v'
            p.write_text('module top(a,y);\nalways @(a) y=a;\nendmodule\n')
            with self.assertRaises(ValueError): audit.parse(p)

    def test_connection_change_is_detected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            a = self.fixture(p/'a.v')
            self.fixture(p/'b.v')
            (p/'b.v').write_text((p/'b.v').read_text().replace('.I0(a)', '.I0(b)'))
            result = audit.compare(a, audit.parse(p/'b.v'), p/'diff.jsonl')
            self.assertEqual(result['counts']['connections_changed'], 1)
            self.assertFalse(result['identical_visible_structure'])

    def test_global_simulation_template_is_separately_hashed(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'a.v'
            before = self.fixture(p)
            with p.open('a') as f:
                f.write('`ifndef GLBL\n`define GLBL\nmodule glbl ();\nendmodule\n`endif\n')
            after = audit.parse(p)
            self.assertEqual(before[0], after[0])
            self.assertNotEqual(before[2]['global_simulation_tail_sha256'], after[2]['global_simulation_tail_sha256'])


if __name__ == '__main__': unittest.main()
