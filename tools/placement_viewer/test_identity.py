import importlib.util,json,unittest
from pathlib import Path
from unittest.mock import patch,MagicMock
p=Path(__file__).parent/'launcher/amf-picker-server.py'
spec=importlib.util.spec_from_file_location('picker',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class IdentityTests(unittest.TestCase):
 def setUp(self):
  m.CACHE.clear();self.row={'run':'/runs/u250','name':'U250','directory':'/runs/u250/placement/visualization'}
 def response(self,directory):
  r=MagicMock();r.__enter__.return_value=r;r.read.return_value=json.dumps({'directory':directory,'frames':[1,2]}).encode();return r
 def test_wrong_design_on_recycled_port_reconnects(self):
  m.CACHE[self.row['run']]='http://127.0.0.1:18767/'
  with patch.object(m.sys,'platform','darwin'),patch.object(m,'urlopen',side_effect=[self.response('/runs/vhk/placement'),self.response(self.row['directory'])]),patch.object(m.subprocess,'run',return_value=MagicMock(returncode=0,stdout='http://127.0.0.1:18771/\n',stderr='')) as connect:
   d=m.open_selected(self.row);self.assertEqual(d['url'],'http://127.0.0.1:18771/');self.assertTrue(d['identity_verified']);self.assertEqual(d['frames'],2);connect.assert_called_once()
 def test_same_design_does_not_restart(self):
  m.CACHE[self.row['run']]='http://127.0.0.1:18767/'
  with patch.object(m,'urlopen',return_value=self.response(self.row['directory'])),patch.object(m.subprocess,'run') as connect:
   self.assertTrue(m.open_selected(self.row)['identity_verified']);connect.assert_not_called()
 def test_dead_cached_service_reconnects(self):
  m.CACHE[self.row['run']]='http://127.0.0.1:18767/'
  with patch.object(m.sys,'platform','darwin'),patch.object(m,'urlopen',side_effect=[OSError('closed'),self.response(self.row['directory'])]),patch.object(m.subprocess,'run',return_value=MagicMock(returncode=0,stdout='http://127.0.0.1:18771/\n',stderr='')):
   self.assertEqual(m.open_selected(self.row)['url'],'http://127.0.0.1:18771/')
 def test_wrong_design_after_reconnect_is_rejected(self):
  with patch.object(m.sys,'platform','darwin'),patch.object(m,'urlopen',return_value=self.response('/runs/vhk/placement')),patch.object(m.subprocess,'run',return_value=MagicMock(returncode=0,stdout='http://127.0.0.1:18771/\n',stderr='')):
   with self.assertRaises(RuntimeError):m.open_selected(self.row)
   self.assertNotIn(self.row['run'],m.CACHE)
 def test_legacy_placement_directory(self):
  row={'run':'/runs/u250','name':'old'}
  with patch.object(m,'urlopen',return_value=self.response('/runs/u250/placement')):
   self.assertIsNotNone(m.matching_service('http://127.0.0.1:18767/',row))
 def test_device_same_but_run_different_is_rejected(self):
  with patch.object(m,'urlopen',return_value=self.response('/runs/u250-other/placement')):
   self.assertIsNone(m.matching_service('http://127.0.0.1:18767/',self.row))
 def test_nonviewer_or_bad_json_rejected(self):
  with patch.object(m,'urlopen',side_effect=ValueError('html')):
   self.assertIsNone(m.matching_service('http://127.0.0.1:18767/',self.row))
if __name__=='__main__':unittest.main(verbosity=2)
