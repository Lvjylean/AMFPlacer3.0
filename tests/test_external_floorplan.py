import copy
import csv
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from import_external_floorplan import prepare, digest, validate_external_inputs


class ExternalFloorplanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.out = self.root / 'normalized'
        self.netlist = self.root / 'netlist.zip'
        with zipfile.ZipFile(self.netlist, 'w') as z:
            z.writestr('allCellPinNet', 'curCell=> a[0] type=> FDRE\ncurCell=> b type=> LUT6\n')
        self.amf = self.root / 'amf.json'
        self.amf.write_text(json.dumps(dict(source_dcp_sha256='same-dcp', output_sha256=digest(self.netlist), amf_cell_count=2)))
        (self.source / 'membership.tsv').write_text(
            'cell\tmodule\tresource\tref\nGND\tCONSTANT\tOTHER\tGND\na[0]\tS0\tFF\tFDRE\nb\tS1\tLUT\tLUT6\n')
        self.regions = {'S0': ['X0Y0', 'X2Y0'], 'S1': ['X0Y0']}
        self.bounds = {'S0': [0, 0, 2, 0], 'S1': [0, 0, 0, 0]}
        (self.source / 'regions.tcl').write_text(''.join(
            'dict set planned_regions ' + m + ' {' + ' '.join(crs) + '}\n' +
            'dict set planned_bounds ' + m + ' {' + ' '.join(map(str, self.bounds[m])) + '}\n'
            for m, crs in self.regions.items()))
        (self.source / 'soft_region_adapter.tcl').write_text('set_property IS_SOFT true $pb\n')
        (self.source / 'core_solution.json').write_text(json.dumps({'regions': {
            m: {'crs': crs} for m, crs in self.regions.items()}}))
        (self.source / 'edge_padding.json').write_text(json.dumps({'changes': [
            dict(module=m, before_crs=crs, after_crs=crs, after_bounds=self.bounds[m])
            for m, crs in self.regions.items()]}))
        self.manifest = dict(input_sha256='same-dcp', module_count=2, counts={'CONSTANT': 1, 'S0': 1, 'S1': 1})
        self.rehash()

    def rehash(self):
        self.manifest['files'] = {p.name: digest(p) for p in self.source.iterdir() if p.name != 'manifest.json'}
        (self.source / 'manifest.json').write_text(json.dumps(self.manifest))

    def prepare(self):
        return prepare(self.source, self.netlist, self.amf, self.out)

    def test_exact_membership_constants_overlap_and_holes(self):
        r = self.prepare()
        self.assertEqual(r['cells'], 2)
        self.assertEqual(r['constant_cells_omitted'], 1)
        self.assertIn('S0\tX0Y0 X2Y0\n', (self.out / 'regions.tsv').read_text())
        self.assertIn('S1\tX0Y0\n', (self.out / 'regions.tsv').read_text())
        self.assertNotIn('X1Y0', (self.out / 'regions.tsv').read_text())
        self.assertEqual((self.out / 'membership.tsv').read_text(), 'cell\tmodule\tref\na[0]\tS0\tFDRE\nb\tS1\tLUT6\n')
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.prepare()

    def test_source_hash_mismatch(self):
        with (self.source / 'membership.tsv').open('a') as f:
            f.write('x\tS0\tLUT\tLUT6\n')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            self.prepare()
        self.assertFalse(self.out.exists())

    def test_different_checkpoint(self):
        self.manifest['input_sha256'] = 'different-dcp'
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'different source DCP'):
            self.prepare()

    def test_duplicate_binding(self):
        p = self.source / 'membership.tsv'
        p.write_text(p.read_text() + 'a[0]\tS1\tFF\tFDRE\n')
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.prepare()
        self.assertFalse(self.out.exists())

    def test_partial_coverage(self):
        p = self.source / 'membership.tsv'
        p.write_text(p.read_text().replace('b\tS1\tLUT\tLUT6\n', ''))
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            self.prepare()

    def test_primitive_mismatch(self):
        p = self.source / 'membership.tsv'
        p.write_text(p.read_text().replace('b\tS1\tLUT\tLUT6', 'b\tS1\tLUT\tLUT5'))
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'primitive mismatch'):
            self.prepare()

    def test_no_tcl_execution(self):
        p = self.source / 'regions.tcl'
        p.write_text(p.read_text() + 'exec touch should-not-exist\n')
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'Unsupported Tcl'):
            self.prepare()
        self.assertFalse((self.root / 'should-not-exist').exists())

    def test_final_soft_geometry_is_checked(self):
        p = self.source / 'edge_padding.json'
        d = json.loads(p.read_text()); d['changes'][0]['after_crs'] = ['X0Y0']
        p.write_text(json.dumps(d)); self.rehash()
        with self.assertRaisesRegex(ValueError, 'region mismatch'):
            self.prepare()

    def test_binary_and_input_identity_gate(self):
        self.prepare()
        cfg = {
            'external floorplan membership file': str(self.out / 'membership.tsv'),
            'external floorplan regions file': str(self.out / 'regions.tsv'),
            'external floorplan manifest file': str(self.out / 'manifest.json'),
            'vivado extracted design information file': str(self.netlist),
        }
        with patch('import_external_floorplan.subprocess.run', return_value=SimpleNamespace(returncode=0, stdout='{}')):
            with self.assertRaisesRegex(ValueError, 'does not support'):
                validate_external_inputs(cfg, 'old-binary')
        with patch('import_external_floorplan.subprocess.run', return_value=SimpleNamespace(
                returncode=0, stdout='{"external_floorplan_initialization_schema":1}')):
            validate_external_inputs(cfg, 'new-binary')
        (self.out / 'regions.tsv').write_text('tampered')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            validate_external_inputs(cfg, 'new-binary')

    def test_absent_mode_and_partial_mode(self):
        validate_external_inputs({}, 'unused')
        with self.assertRaisesRegex(ValueError, 'requires'):
            validate_external_inputs({'external floorplan regions file': 'somewhere'}, 'unused')


if __name__ == '__main__':
    unittest.main()

