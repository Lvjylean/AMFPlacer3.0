import gzip,json,struct,tempfile,unittest
from pathlib import Path
from group_tracking import GroupTracker

class TrackingTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);c=self.root/'.viewer-binary-v1';c.mkdir()
  d={'names':['mod/a','mod/b','mod2/c','mod/virtual(4)'],'types':['LUT','FF','DSP','VIRTUAL']}
  (c/'dictionary.json.gz').write_bytes(gzip.compress(json.dumps(d).encode()))
  (c/'manifest.json').write_text(json.dumps({'signature':{'frames':[['one.gz',1,1],['two.gz',1,1]]}}))
  for name,ids in [('one.gz',[0,1,2,3]),('two.gz',[3,2,1,0])]:
   (c/(name+'.bin.gz')).write_bytes(gzip.compress(b''.join(struct.pack('<Iff',i,i+0.25,i+1.5) for i in ids)))
  self.tracker=GroupTracker(self.root)
 def tearDown(self):self.temp.cleanup()
 def test_prefix_delimiter_and_virtual(self):
  g=self.tracker.select({'prefix':'mod/'});self.assertEqual(g['ids'],[0,1]);self.assertEqual(g['excluded_virtual'],1)
  self.assertEqual(self.tracker.select({'prefix':'mod','include_virtual':True})['ids'],[0,1,3])
 def test_exact_duplicates_missing(self):
  g=self.tracker.select({'cells':['mod/a','mod/a','unknown','mod/virtual(4)']})
  self.assertEqual(g['ids'],[0]);self.assertEqual(g['duplicates'],1);self.assertEqual(g['missing'],['unknown']);self.assertEqual(g['excluded_virtual'],1)
 def test_identity_survives_reorder_and_read_only(self):
  g=self.tracker.select({'cells':['mod/a','mod/b']});before={str(p):p.stat().st_mtime_ns for p in self.root.rglob('*') if p.is_file()}
  for name in ('one.gz','two.gz'):
   records=list(struct.iter_unpack('<Iff',gzip.decompress(self.tracker.frame(g['token'],name))))
   self.assertEqual({x[0] for x in records},{0,1});self.assertTrue(all(x==i+0.25 and y==i+1.5 for i,x,y in records))
  self.assertEqual(before,{str(p):p.stat().st_mtime_ns for p in self.root.rglob('*') if p.is_file()})
 def test_reject_empty_and_path(self):
  for spec in ({'prefix':''},{'cells':['missing']},{'prefix':'mod','cells':[]},{'cells':[2]}):
   with self.assertRaises(ValueError):self.tracker.select(spec)
  g=self.tracker.select({'prefix':'mod'})
  with self.assertRaises(ValueError):self.tracker.frame(g['token'],'../../etc/passwd')
if __name__=='__main__':unittest.main()
