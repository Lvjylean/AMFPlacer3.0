from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ImportAcceptance(unittest.TestCase):
    def check_metrics(self, **changes):
        metrics=dict(requested=10,present=10,placed=10,exact_loc_bel_matches=10,
                     exact_original_loc_bel_matches=10,rejection_events=0,
                     srl_violations=0,cascade_violations=0)
        metrics.update(changes)
        body='source {'+str(ROOT/'scripts/import_acceptance.tcl')+'}\n'
        body+='set metrics [dict create '+' '.join(f'{k} {v}' for k,v in metrics.items())+']\n'
        body+='if {[catch {require_legal_amf_import $metrics} msg]} {puts $msg;exit 2}\n'
        return subprocess.run(['tclsh'],input=body,text=True,capture_output=True)

    def test_exact_import_passes(self):
        self.assertEqual(self.check_metrics().returncode,0)

    def test_composite_ram_anchor_requires_exact_physical_footprint(self):
        valid=['RAM32X1D','SLICE_X1Y2','H6LUT','SLICE_X1Y2','G6LUT',
               'SLICE_X1Y2/G6LUT SLICE_X1Y2/H6LUT']
        cases=[(valid,1)]
        for index,replacement in [(0,'RAM32X1S'),(1,'SLICE_X2Y2'),(2,'F6LUT'),
                                   (3,'SLICE_X1Y3'),(4,'E6LUT'),
                                   (5,'SLICE_X1Y2/G6LUT'),
                                   (5,'SLICE_X1Y2/G6LUT SLICE_X1Y2/F6LUT'),
                                   (5,'SLICE_X1Y2/G6LUT SLICE_X1Y2/H6LUT SLICE_X1Y2/F6LUT')]:
            changed=valid.copy();changed[index]=replacement;cases.append((changed,0))
        for fields,expected in cases:
            with self.subTest(fields=fields):
                body='source {'+str(ROOT/'scripts/import_acceptance.tcl')+'}\n'
                body+='puts [amf3_ram32x1d_anchor_match '+' '.join('{'+f+'}' for f in fields)+']\n'
                result=subprocess.run(['tclsh'],input=body,text=True,capture_output=True,check=True)
                self.assertEqual(result.stdout.strip(),str(expected),result.stderr)

    def test_every_failure_dimension_stops_before_backend(self):
        for key,value in dict(present=9,placed=9,exact_loc_bel_matches=9,
                              exact_original_loc_bel_matches=9,rejection_events=1,
                              srl_violations=1,cascade_violations=1).items():
            with self.subTest(key=key):
                result=self.check_metrics(**{key:value})
                self.assertEqual(result.returncode,2)
                self.assertIn(key,result.stdout)
