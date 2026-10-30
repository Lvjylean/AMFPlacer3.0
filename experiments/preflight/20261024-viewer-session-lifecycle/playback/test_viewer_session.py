import json
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.request import Request,urlopen
from urllib.error import HTTPError
from viewer_session import PageLeases,LeaseHandler,serve

class SessionTests(unittest.TestCase):
    def setUp(self):
        self.t=0;self.p=PageLeases(now=lambda:self.t,token='secret')
    def ping(self,client='a',close=False):self.p.update(dict(id=client,token='secret',close=close))
    def test_never_opened_expires(self):
        self.t=179;self.assertFalse(self.p.expired());self.t=180;self.assertTrue(self.p.expired())
    def test_last_page_close_grace(self):
        self.ping();self.t=10;self.ping(close=True);self.t=39;self.assertFalse(self.p.expired());self.t=40;self.assertTrue(self.p.expired())
    def test_other_page_preserves_service(self):
        self.ping('a');self.ping('b');self.ping('a',True);self.t=31;self.assertFalse(self.p.expired());self.ping('b');self.t=180;self.assertFalse(self.p.expired())
    def test_crash_ttl(self):
        self.ping();self.t=100;self.ping();self.t=279;self.assertFalse(self.p.expired());self.t=280;self.assertTrue(self.p.expired())
    def test_health_probe_does_not_renew(self):
        self.ping();self.ping(close=True)
        for i in range(31):self.t=i;self.p.info()
        self.assertTrue(self.p.expired())
    def test_refresh_during_grace(self):
        self.ping();self.ping(close=True);self.t=20;self.ping('new-document');self.t=31;self.assertFalse(self.p.expired())
    def test_bad_token_and_unknown_close(self):
        with self.assertRaises(ValueError):self.p.update(dict(id='a',token='wrong'))
        self.t=179;self.ping('unknown',True);self.t=180;self.assertTrue(self.p.expired())
    def test_owner_loss(self):
        self.ping();self.p.stop.set();self.assertTrue(self.p.expired())
    def test_injection_beacon_and_distinct_page_ids(self):
        s=self.p.inject(b'<html></html>').decode()
        self.assertIn('crypto.randomUUID()',s);self.assertIn('pagehide',s);self.assertIn('sendBeacon',s);self.assertIn('15000',s)
    def test_live_http_leases_and_shutdown(self):
        pool=PageLeases(ttl=10,grace=.05,token='test-token')
        class Base(BaseHTTPRequestHandler):
            def do_GET(self):
                body=pool.inject(b'<html>test</html>');self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
            def do_POST(self):self.send_error(404)
            def log_message(self,*args):pass
        class Handler(LeaseHandler,Base):pass
        Handler.lifecycle=pool
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=serve,args=(server,pool));thread.start()
        root='http://127.0.0.1:'+str(server.server_port)
        try:
            with urlopen(root+'/') as r:self.assertIn(b'sendBeacon',r.read())
            with urlopen(root+'/api/viewer-lifecycle') as r:self.assertEqual(json.load(r)['active_pages'],0)
            for client,close in [('one',False),('two',False),('one',True)]:
                with urlopen(Request(root+'/api/viewer-lease',data=json.dumps(dict(token=pool.token,id=client,close=close)).encode())) as r:self.assertEqual(r.status,204)
            self.assertEqual(pool.active(),1)
            with self.assertRaises(HTTPError) as cm:urlopen(Request(root+'/api/viewer-lease',data=b'{"token":"bad","id":"a"}'))
            self.assertEqual(cm.exception.code,400)
            with urlopen(Request(root+'/api/viewer-lease',data=json.dumps(dict(token=pool.token,id='two',close=True)).encode())) as r:r.read()
            thread.join(timeout=3);self.assertFalse(thread.is_alive())
        finally:
            pool.stop.set()
            if thread.is_alive():server.shutdown();thread.join()

if __name__=='__main__':unittest.main()
