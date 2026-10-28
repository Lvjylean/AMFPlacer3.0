import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/diagnostics'))
from verify_minimap2_logic_rewrites import Design,verify


class LutReduction(unittest.TestCase):
    def check(self, init):
        base='''module minimap2_functional(a,y);
input a;
output y;
LUT2 #(.INIT(4'h8)) g (.I0(a),.I1(a),.O(y));
endmodule
'''
        result=base.replace("LUT2 #(.INIT(4'h8)) g (.I0(a),.I1(a),.O(y))",f"LUT1 #(.INIT(2'h{init})) g (.I0(a),.O(y))")
        with tempfile.TemporaryDirectory() as d:
            r=Path(d);(r/'a.v').write_text(base);(r/'b.v').write_text(result)
            return verify(Design(r/'a.v'),Design(r/'b.v'),r)

    def test_redundant_lut_input_can_be_removed(self):
        report=self.check('2')
        self.assertTrue(report['visible_structural_certificate_passed'])
        self.assertEqual(len(report['lut_truth_table_rewrites']),1)

    def test_wrong_reduced_truth_table_fails(self):
        self.assertFalse(self.check('1')['visible_structural_certificate_passed'])


if __name__=='__main__':unittest.main()
