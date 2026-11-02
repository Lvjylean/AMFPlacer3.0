import json
from pathlib import Path
import sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from full_ff_controls import materialize
from full_design_metadata import validate_binary,validate_ff_binding

class Controls(unittest.TestCase):
    def fixture(self,r):
        (r/'cells.tsv').write_text('cell_name\tprimitive\na\tFDRE\nb\tFDPE\nlogic\tLUT1\n')
        (r/'ff_control_counts.tsv').write_text('primitive\tcount\nFDRE\t1\nFDSE\t0\nFDCE\t0\nFDPE\t1\nLDCE\t0\nAND2B1L\t0\n')
        (r/'ff_control_inversions.tsv').write_text('cell\tprimitive\tpin\tinverted\nb\tFDPE\tPRE\t1\n')
    def test_complete_defaults_and_actual_pin_polarity(self):
        with tempfile.TemporaryDirectory() as d:
            r=Path(d);self.fixture(r);result=materialize(r,r/'cells.tsv',r/'out')
            self.assertEqual(result['polarities'],{'0,0,0':1,'0,1,0':1})
            self.assertIn('a FDRE 0 0 0\n',(r/'out').read_text())
    def test_count_or_pin_mismatch_fails(self):
        for change in ('count','pin','duplicate'):
            with tempfile.TemporaryDirectory() as d:
                r=Path(d);self.fixture(r)
                if change=='count':
                    p=r/'ff_control_counts.tsv';p.write_text(p.read_text().replace('FDRE\t1','FDRE\t2'))
                else:
                    p=r/'ff_control_inversions.tsv';p.write_text(p.read_text()+('b\tFDPE\tPRE\t1\n' if change=='duplicate' else 'a\tFDRE\tCE\t1\n'))
                with self.assertRaises(ValueError):materialize(r,r/'cells.tsv',r/'out')
    def test_old_binary_cannot_ignore_metadata(self):
        with patch('subprocess.check_output',return_value=json.dumps({'full_interface_adaptation_schema':1})):
            with self.assertRaisesRegex(ValueError,'polarity'):validate_binary({'FF control polarities file':'controls'},'binary')
    def test_full_adapter_requires_metadata(self):
        with patch('subprocess.check_output',return_value=json.dumps({'full_interface_adaptation_schema':1,'ff_control_polarity_schema':1})):
            with self.assertRaisesRegex(ValueError,'requires complete'):validate_binary({'CompositeIOTimingBoundary':'true'},'binary')
    def test_special_latch_resource_uses_sri_polarity(self):
        with tempfile.TemporaryDirectory() as d:
            r=Path(d);self.fixture(r)
            with (r/'cells.tsv').open('a') as f:f.write('gate\tAND2B1L\n')
            p=r/'ff_control_counts.tsv';p.write_text(p.read_text().replace('AND2B1L\t0','AND2B1L\t1'))
            with (r/'ff_control_inversions.tsv').open('a') as f:f.write('gate\tAND2B1L\tSRI\t1\n')
            result=materialize(r,r/'cells.tsv',r/'out')
            self.assertEqual(result['cells'],3)
            self.assertIn('gate AND2B1L 0 1 0\n',(r/'out').read_text())
    def test_metadata_cannot_change_after_binding(self):
        with tempfile.TemporaryDirectory() as d:
            r=Path(d);self.fixture(r);result=materialize(r,r/'cells.tsv',r/'out')
            cfg={'FF control polarities file':str(r/'out')};record={'ff_control_polarities':result}
            validate_ff_binding(cfg,record)
            (r/'out').write_text('changed')
            with self.assertRaisesRegex(ValueError,'not bound'):validate_ff_binding(cfg,record)

if __name__=='__main__':unittest.main()
