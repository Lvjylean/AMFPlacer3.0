import copy,json,unittest
from pathlib import Path
from boundary_view import display_layers,merge_segments

class BoundaryViewTests(unittest.TestCase):
 def test_deduplicated_edges_and_seam_not_perimeter(self):
  g=dict(clock_regions=[dict(name='a',bounds=[1,5,0,4]),dict(name='b',bounds=[1,5,4,8]),dict(name='io',bounds=[0,1,0,4],slice_count=0)],slr_regions=[dict(name='SLR0',bounds=[0,5,0,4]),dict(name='SLR1',bounds=[0,5,4,8])]);before=copy.deepcopy(g);d=display_layers(g)
  self.assertEqual(g,before);self.assertEqual(d['fabric_cr'],2);self.assertEqual(d['total_cr'],3);self.assertEqual(d['seams'],[dict(y=4,x0=0,x1=5,label='SLR0 / SLR1')]);self.assertEqual(len(d['edges']),5)
 def test_slr_gap_is_not_faked(self):
  with self.assertRaises(ValueError):display_layers(dict(clock_regions=[],slr_regions=[dict(name='a',bounds=[0,1,0,1]),dict(name='b',bounds=[0,1,2,3])]))
if __name__=='__main__':unittest.main()
