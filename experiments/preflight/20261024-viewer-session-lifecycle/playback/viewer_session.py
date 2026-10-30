"""Count pages, not HTTP probes. Closing the last page expires a service."""
import json
import secrets
import threading
import time
from urllib.parse import urlsplit

class PageLeases:
    def __init__(self, ttl=180, grace=30, now=time.monotonic, token=None):
        self.ttl,self.grace,self.now=ttl,grace,now
        self.token=token or secrets.token_hex(32)
        self.clients={};self.deadline=now()+ttl;self.lock=threading.RLock()
        self.stop=threading.Event()
    def update(self, data):
        if not isinstance(data,dict):raise ValueError('Invalid lease')
        client=data.get('id')
        if data.get('token')!=self.token or not isinstance(client,str) or not 1<=len(client)<=80:
            raise ValueError('Invalid lease')
        with self.lock:
            if data.get('close') is True:
                if client in self.clients:
                    del self.clients[client]
                    if not self.clients:self.deadline=self.now()+self.grace
            else:
                self.clients[client]=self.now()
    def active(self):
        with self.lock:
            now=self.now()
            self.clients={k:v for k,v in self.clients.items() if now-v<self.ttl}
            return len(self.clients)
    def expired(self):
        return self.stop.is_set() or (not self.active() and self.now()>=self.deadline)
    def info(self):
        return dict(version=2,mode='page-leased',ttl_seconds=self.ttl,close_grace_seconds=self.grace,active_pages=self.active())
    def inject(self,body):
        script='''<script>(()=>{const id=crypto.randomUUID(),token=TOKEN;
const ping=(close=false)=>{const body=JSON.stringify({id,token,close});if(close)navigator.sendBeacon('/api/viewer-lease',body);else fetch('/api/viewer-lease',{method:'POST',body,cache:'no-store'}).catch(()=>{});};
ping();setInterval(()=>ping(),15000);addEventListener('pageshow',()=>ping());addEventListener('pagehide',()=>ping(true));document.addEventListener('visibilitychange',()=>{if(!document.hidden)ping();});})();</script>'''.replace('TOKEN',json.dumps(self.token))
        return body+script.encode()

class LeaseHandler:
    def do_GET(self):
        if urlsplit(self.path).path!='/api/viewer-lifecycle':return super().do_GET()
        self.lease_reply(json.dumps(self.lifecycle.info()).encode(),200)
    def do_POST(self):
        if urlsplit(self.path).path!='/api/viewer-lease':return super().do_POST()
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=1024:raise ValueError('Invalid size')
            self.lifecycle.update(json.loads(self.rfile.read(size)))
            self.lease_reply(b'',204)
        except (ValueError,TypeError):self.lease_reply(b'Invalid lease',400)
    def lease_reply(self,body,code):
        self.send_response(code);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store');self.end_headers()
        try:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass

def serve(server,lifecycle):
    done=threading.Event()
    def monitor():
        while not done.wait(1):
            if lifecycle.expired():
                print('VIEWER_SESSION: no live pages; exiting',flush=True)
                server.shutdown();return
    threading.Thread(target=monitor,daemon=True).start()
    try:server.serve_forever(poll_interval=.25)
    finally:done.set();server.server_close()
