import copy,json,unittest
from pathlib import Path
from hard_resources import attach_inventory

class HardResourceTests(unittest.TestCase):
    def setUp(self):
        self.data=json.loads(Path(__file__).with_name('hard_resource_inventory.json').read_text())

    def test_scoped_to_same_part_and_device_geometry(self):
        for g in [dict(part='xcu250',device_input_sha256=self.data['device_input_sha256']),
                  dict(part=self.data['part'],device_input_sha256='different')]:
            before=copy.deepcopy(g);attach_inventory(g);self.assertEqual(g,before)
        g=dict(part=self.data['part'],device_input_sha256=self.data['device_input_sha256'],bounds=[1,2,3,4])
        before=copy.deepcopy(g);attach_inventory(g)
        self.assertEqual({k:v for k,v in g.items() if k!='hard_resources'},before)
        self.assertEqual(g['hard_resources'],self.data)

    def test_actual_site_inventory_and_anchor_membership(self):
        counts={};names=set()
        for a in self.data['regions']:
            n=0
            for r in a['resources']:
                for s in r['sites']:
                    self.assertNotIn(s['site'],names);names.add(s['site']);n+=1
                    self.assertFalse(s['type'].startswith('SLICE'))
                    x,y=s['position']
                    self.assertTrue(any(l<=x<=h and b<=y<=t for l,h,b,t in a['bands']))
                    self.assertTrue(any(s['tile']==p['tile'] and s['position']==p['position'] and s['site'] in p['sites'] for p in r['anchors']))
            counts[a['id']]=n
        self.assertEqual(counts,{'upper':12,'lower':31})
        self.assertEqual(len(names),43)

if __name__=='__main__':unittest.main()
