#!/usr/bin/env python3
"""Mutation checks for the audit (not tests of the placer implementation)."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import sys

import compare_functional_edif as audit
import classify_edif_audit_diffs as classify
import compare_native_logic_audit as native

BASE = '''(edif top (status (written (timeStamp 1))) (library work
(cell top (cellType GENERIC) (view netlist (interface
(port I (direction INPUT)) (port O (direction OUTPUT))) (contents
(instance u (viewref netlist (cellref LUT1 (libraryref hdi_primitives)))
(property INIT (string "2'h2")) (property SOFT_HLUTNM (string "pair1")))
(net n (joined (portref I) (portref I0 (instanceref u))))
(net q (joined (portref O) (portref O (instanceref u)))))))))'''


class AuditMutationTest(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith('linux'), 'native ledger uses GNU sort on the server')
    def test_native_mutations(self):
        tables = {
            'parameters': "cell\ttype\tparameter\tvalue\nu\tLUT1\tINIT\t2'h2\n",
            'pins': 'pin\tdirection\tconnected\tinverted\tref_pin\nu/I0\tIN\t1\t0\tI0\nu/O\tOUT\t1\t0\tO\n',
            'nets': 'net\tpins...\nn\tu/I0\ttop/I\n',
            'ports': 'port\tdirection\tnets...\nI\tIN\tn\n',
        }
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            root=Path(tmp)
            for variant in ['base','order','mutation']:
                d=root/variant;d.mkdir();(d/'complete.tsv').write_text('complete\n')
                for name,text in tables.items():
                    if variant=='order' and name=='nets':text=text.replace('u/I0\ttop/I','top/I\tu/I0')
                    if variant=='mutation' and name=='parameters':text=text.replace("2'h2","2'h1")
                    (d/(name+'.tsv')).write_text(text)
                native.normalize(d)
            for variant in ['order','mutation']:
                out=root/(variant+'.json');native.compare(root/'base',root/variant,out)
                self.assertEqual(json.loads(out.read_text())['all_tables_identical'],variant=='order')

    def test_full_ledger_mutations(self):
        variants = {
            'reorder': BASE.replace('(portref I) (portref I0 (instanceref u))', '(portref I0 (instanceref u)) (portref I)').replace('timeStamp 1','timeStamp 2'),
            'init': BASE.replace("2'h2", "2'h1"),
            'rewire': BASE.replace('portref I0', 'portref I1'),
            'type': BASE.replace('cellref LUT1', 'cellref LUT2'),
            'missing_net': BASE.replace('(net q (joined (portref O) (portref O (instanceref u))))', ''),
            'direction': BASE.replace('direction INPUT', 'direction OUTPUT'),
            'new_unknown_property': BASE.replace('(property INIT', '(property UNKNOWN_FUNCTION (integer 1)) (property INIT'),
            'packing_hint': BASE.replace('(property SOFT_HLUTNM (string "pair1"))', ''),
        }
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            root = Path(tmp)
            for name, text in {'base':BASE, **variants}.items():
                src = root/(name+'.edf'); src.write_text(text)
                audit.export_ledger(src,root/(name+'.db'))
            for name in variants:
                diffs = root/(name+'.jsonl')
                audit.compare(root/'base.db',root/(name+'.db'),diffs)
                result = json.loads(diffs.with_suffix('.summary.json').read_text())
                self.assertEqual(result['exact_structure_and_all_properties_equal'], name=='reorder', name)
                classify.main(diffs)
                result = json.loads(diffs.with_suffix('.classification.json').read_text())
                self.assertEqual(result['structural_equivalence_pass'], name in {'reorder','packing_hint'}, name)
            bad = root/'bad.edf'; bad.write_text(BASE[:-1])
            with self.assertRaises(ValueError):
                audit.export_ledger(bad,root/'bad.db')


if __name__ == '__main__':
    unittest.main()
