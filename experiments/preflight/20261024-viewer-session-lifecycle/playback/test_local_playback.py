import json
import gzip
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.request import urlopen, Request
from urllib.error import HTTPError
from local_playback import Mirror, handler


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.mirror=Mirror(dict(cache=str(self.root),remote_cache='/case/cache',limit_bytes=1024**2))
        self.mirror.frames={'frames':['a.gz','b.gz','c.gz']}
        self.image=b'RIFF0000WEBP'+b'x'*(190*1024)

    def tearDown(self):self.tmp.cleanup()

    def test_local_lru_and_disk_quota(self):
        for name in self.mirror.frames['frames']:self.mirror.put(name,self.image,['id',[1,2]])
        self.assertLessEqual(self.mirror.used(),self.mirror.limit)
        self.assertIn('a.gz',self.mirror.entries)
        self.assertNotIn('b.gz',self.mirror.entries)
        self.assertEqual(self.mirror.request('c.gz'),self.image)

    def test_pending_request_is_nonblocking_and_bounded(self):
        self.assertIsNone(self.mirror.request('b.gz'))
        self.assertEqual(list(self.mirror.requested),['b.gz'])
        self.assertTrue(self.mirror.wake.is_set())
        with self.assertRaises(ValueError):self.mirror.request('../secret')

    def test_only_webp_may_be_cached(self):
        with self.assertRaises(ValueError):self.mirror.put('a.gz',b'{"state":"pending"}',[])

    def test_cache_reopens_only_for_same_run(self):
        self.mirror.put('a.gz',self.image,['id',[1,2]])
        again=Mirror(self.mirror.config);self.assertIn('a.gz',again.entries)
        with self.assertRaisesRegex(ValueError,'another run'):
            Mirror(dict(self.mirror.config,remote_cache='/other/cache'))

    def test_detail_proxy_preserves_compression_and_post(self):
        payload=gzip.compress(b'{"names":["cell_a"]}')
        class Upstream(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                self.send_response(200);self.send_header('Content-Encoding','gzip')
                self.end_headers();self.wfile.write(payload)
            def do_POST(self):
                body=self.rfile.read(int(self.headers['Content-Length']))
                self.send_response(200);self.end_headers();self.wfile.write(body)
        upstream=ThreadingHTTPServer(('127.0.0.1',0),Upstream)
        self.mirror.config['upstream']='http://127.0.0.1:'+str(upstream.server_port)
        local=ThreadingHTTPServer(('127.0.0.1',0),handler(self.mirror))
        servers=(upstream,local)
        for server in servers:threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            base='http://127.0.0.1:'+str(local.server_port)
            with urlopen(base+'/api/live-dictionary') as response:
                self.assertEqual(response.headers['Content-Encoding'],'gzip')
                self.assertEqual(response.read(),payload)
            with urlopen(Request(base+'/api/group-selection',data=b'{"cell":"a"}',method='POST')) as response:
                self.assertEqual(response.read(),b'{"cell":"a"}')
            with urlopen(base+'/api/preview?name=b.gz') as response:
                self.assertEqual(response.status,202)
        finally:
            for server in servers:server.shutdown();server.server_close()

    def complete_cache(self):
        names=['a.gz','b.gz']
        self.mirror.frames={'frames':names,'directory':'/case/visualization'}
        for name in names:
            self.mirror.put(name,b'RIFF0000WEBPtest',['identity',[10,20]])
            self.mirror.entries[name]['sha256']=hashlib.sha256(self.mirror.path(name).read_bytes()).hexdigest()
        self.mirror.save()
        (self.root/'frames.json').write_text(json.dumps(self.mirror.frames))
        (self.root/'metadata.json').write_text(json.dumps({'source':'/case/config.json'}))
        return dict(self.mirror.config,preview_local_only=True,upstream='http://127.0.0.1:1')

    def test_complete_cache_is_readonly_and_no_network(self):
        config=self.complete_cache()
        with patch('local_playback.urlopen',side_effect=AssertionError('Unexpected network')):
            local=Mirror(config);local.sync()
            self.assertEqual(local.request('b.gz'),b'RIFF0000WEBPtest')
            self.assertTrue(local.status()['local_complete_cache'])
            with self.assertRaises(ValueError):local.put('b.gz',b'RIFF0000WEBPtest',[])
            with self.assertRaises(ValueError):local.fetch('/api/metadata')
            with self.assertRaises(ValueError):local.evict(9999999)
        self.assertEqual(set(local.entries),{'a.gz','b.gz'})

    def test_complete_cache_rejects_missing_or_corrupt_frame(self):
        config=self.complete_cache()
        target=self.mirror.path('b.gz');body=target.read_bytes();target.write_bytes(body[:-1]+b'x')
        with self.assertRaisesRegex(ValueError,'校验失败'):Mirror(config)
        target.unlink()
        with self.assertRaisesRegex(ValueError,'不完整'):Mirror(config)

    def test_complete_http_preview_never_proxies_coordinates(self):
        local=Mirror(self.complete_cache())
        server=ThreadingHTTPServer(('127.0.0.1',0),handler(local))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        base='http://127.0.0.1:'+str(server.server_port)
        try:
            with patch('local_playback.urlopen',side_effect=AssertionError('Unexpected upstream')):
                for name in local.frames['frames']:
                    with urlopen(base+'/api/preview?name='+name) as r:
                        self.assertEqual(r.status,200);self.assertEqual(r.read(),b'RIFF0000WEBPtest')
                with urlopen(base+'/api/viewer-identity') as r:
                    identity=json.load(r);self.assertTrue(identity['local_complete_cache'])
                    self.assertNotIn('full_coordinates',identity['capabilities'])
                with self.assertRaises(HTTPError) as error:urlopen(base+'/api/live-dictionary')
                self.assertEqual(error.exception.code,409)
        finally:server.shutdown();server.server_close()


if __name__=='__main__':unittest.main()
