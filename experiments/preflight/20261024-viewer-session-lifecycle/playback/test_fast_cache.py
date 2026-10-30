import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from compact_preview import progressive_order, CompactPreview, Budget
from local_playback import Mirror
from test_compact_preview import PreviewTests

class FastCacheTests(unittest.TestCase):
    def test_progressive_coverage_and_all_frames(self):
        for count in [0,1,2,3,449,470]:
            names=list(range(count));order=progressive_order(names)
            self.assertEqual(sorted(order),names)
            if count>2:
                self.assertEqual(order[:3],[0,count-1,(count-1)//2])
                self.assertLessEqual(max(b-a for a,b in zip(sorted(order[:9]),sorted(order[:9])[1:])),(count+7)//8)

    def test_budget_limits(self):
        for cpu,rate in [(0,2),(1.1,2),(.5,0)]:
            with self.assertRaises(ValueError):Budget(cpu,rate)
        Budget(1,16)

    def test_http_sync_preserves_identity_and_no_ssh(self):
        with tempfile.TemporaryDirectory() as directory:
            m=Mirror(dict(cache=directory,remote_cache='test-run',upstream='http://127.0.0.1:1',manifest_api=True,limit_bytes=64*1024**2))
            name='DumpAllCoordTrace-0.gz';body=b'RIFF1234WEBPdata'
            responses={'/api/frames':dict(frames=[name],directory='test-run/visualization'),
                       '/api/metadata':dict(source='test-run/config.json'),
                       '/api/preview-manifest':dict(index=dict(identity='run-one',entries={name:dict(source=[5,6],bytes=len(body))}),state=dict(ready=1,state='idle'))}
            def fetch(url):return 200,body if url.startswith('/api/preview?') else json.dumps(responses[url]).encode()
            with patch.object(m,'fetch',side_effect=fetch),patch('local_playback.subprocess.check_output',side_effect=AssertionError('SSH must not be used')):
                m.sync();self.assertEqual(m.request(name),body)
                responses['/api/preview-manifest']['index']=dict(identity='run-two',entries={})
                m.sync();self.assertNotIn(name,m.entries)

    def test_reuses_existing_image_at_new_speed(self):
        case=PreviewTests();case.setUp()
        try:
            cache=case.cache;name=case.sources[0].name
            cache.publish(name,b'previous-image',{},cache.signature(name))
            new=CompactPreview(case.sources,cache.types,case.meta,cache.root,case.config,limit_bytes=64*1024**2,start_worker=False,cpu_fraction=1,read_mib_s=16,progressive=True)
            self.assertEqual(new.request(name),b'previous-image')
            self.assertEqual(new.manifest()['index']['identity'],cache.identity)
            self.assertEqual(new.status()['cpu_target'],1)
        finally:case.tearDown()

if __name__=='__main__':unittest.main()
