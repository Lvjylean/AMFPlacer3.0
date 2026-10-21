import importlib.util,json,tempfile,unittest,zipfile
from pathlib import Path
P=Path(__file__).parent/'launcher/device_catalog.py'
spec=importlib.util.spec_from_file_location('catalog',P);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CatalogTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
 def run_dir(self,name,config):
  p=self.root/'experiments/runs'/name;p.mkdir(parents=True);(p/'config.json').write_text(json.dumps(config));return p
 def dcp(self,p,part):
  p.parent.mkdir(parents=True,exist_ok=True)
  with zipfile.ZipFile(p,'w') as z:z.writestr('dcp.xml',f'<Checkpoint><Part Name="{part}"/><Top Name="test"/></Checkpoint>')
 def test_dcp_actual_part_over_misleading_name(self):
  r=self.run_dir('vhk-looking-name',{});self.dcp(r/'reports/design_routed.dcp','xcu250-figd2104-2L-e');row=m.collect(self.root)['runs'][0];self.assertEqual(row['device'],'U250');self.assertEqual(row['frames'],0);self.assertTrue(row['artifacts'][0]['verified'])
 def test_metadata_only_trajectory(self):
  r=self.run_dir('case',{'device':'VHK158','physical device part':'xcvh1582-vsva3697-2MP-e-S'});p=r/'placement/visualization';p.mkdir(parents=True);(p/'DumpAllCoordTrace-0.gz').write_bytes(b'x');row=m.collect(self.root)['runs'][0];self.assertEqual(row['device'],'VHK158');self.assertEqual(row['frames'],1);self.assertEqual(row['artifacts'],[])
 def test_conflict_requires_review(self):
  self.assertEqual(m.classify({'device':'U250'},[{'verified':True,'part':'xcvh1582-vsva3697-2MP-e-S'}])['device'],'UNCLASSIFIED')
 def test_unknown_never_guessed_from_path(self):
  r=self.run_dir('u250-name',{});p=r/'placement';p.mkdir();(p/'DumpAllCoordTrace-0.gz').write_bytes(b'x');self.assertEqual(m.collect(self.root)['runs'][0]['device'],'UNCLASSIFIED')
 def test_bad_zip_reported(self):
  r=self.run_dir('old',{'device':'U250'});p=r/'reports/bad.dcp';p.parent.mkdir();p.write_bytes(b'bad');d=m.collect(self.root)['runs'][0];self.assertFalse(d['artifacts'][0]['verified']);self.assertIn('error',d['artifacts'][0])
 def test_legacy_directory_and_new_priority(self):
  r=self.run_dir('old',{'device':'U250'});p=r/'placement';p.mkdir();(p/'DumpAllCoordTrace-0.gz').write_bytes(b'x');new=p/'visualization';new.mkdir();(new/'DumpAllCoordTrace-0.gz').write_bytes(b'y');d=m.collect(self.root)['runs'][0];self.assertEqual(d['frames'],1);self.assertEqual(d['directory'],str(new))
 def test_external_link_only_matches_exact_run(self):
  r=self.run_dir('a',{'device':'VHK158'});self.run_dir('b',{'device':'VHK158'});link={'run':str(r),'artifact':{'name':'base_routed.dcp','part':'xcvh1582-vsva3697-2MP-e-S','verified':True,'host':'eda072','path':'/remote/base_routed.dcp'}};rows=m.collect(self.root,[link])['runs'];self.assertEqual(len(rows),1);self.assertEqual(rows[0]['run'],str(r));self.assertEqual(rows[0]['artifacts'][0]['host'],'eda072')
 def test_unsupported_device_keeps_actual_part(self):
  self.assertEqual(m.classify({},[{'part':'xc-other','verified':True}])['device'],'xc-other')
if __name__=='__main__':unittest.main(verbosity=2)
