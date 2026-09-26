import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from srl_cascades import correct_and_validate


class SRLCascades(unittest.TestCase):
    def test_b_to_a_dedicated_chain_is_preserved(self):
        a={'s':'SLICE_X0Y0/B6LUT','t':'SLICE_X0Y0/A6LUT'}
        self.assertEqual(correct_and_validate(a,{'s':'SRLC32E'},[('s','t')]),(a,[]))

    def test_a_output_can_cross_sites(self):
        a={'s':'SLICE_X0Y0/A6LUT','t':'SLICE_X10Y800/H6LUT'}
        self.assertEqual(correct_and_validate(a,{'s':'SRLC32E'},[('s','t')]),(a,[]))

    def test_standalone_h_source_moves_only_within_its_site(self):
        a={'s':'SLICE_X0Y0/H6LUT','t':'SLICE_X10Y800/H6LUT'}
        fixed,changes=correct_and_validate(a,{'s':'SRLC32E'},[('s','t')])
        self.assertEqual(fixed,dict(a,s='SLICE_X0Y0/A6LUT'))
        self.assertEqual(changes[0]['before'],a['s'])
        self.assertEqual(a['s'],'SLICE_X0Y0/H6LUT')

    def test_shared_h_to_g_chain_is_preserved(self):
        a={'s':'SLICE_X0Y0/H6LUT','t':'SLICE_X0Y0/G6LUT'}
        self.assertEqual(correct_and_validate(a,{'s':'SRLC32E'},[('s','t')]),(a,[]))

    def test_occupied_a_exit_is_not_overwritten(self):
        a={'s':'SLICE_X0Y0/H6LUT','other':'SLICE_X0Y0/A6LUT','t':'SLICE_X1Y0/H6LUT'}
        with self.assertRaisesRegex(ValueError,'Illegal dedicated'):
            correct_and_validate(a,{'s':'SRLC32E'},[('s','t')])

    def test_wrong_internal_direction_is_rejected(self):
        a={'s':'SLICE_X0Y0/B6LUT','t':'SLICE_X0Y0/C6LUT'}
        with self.assertRaisesRegex(ValueError,'Illegal dedicated'):
            correct_and_validate(a,{'s':'SRLC32E'},[('s','t')])
