"""Small Mac preview mirror; large coordinate queries remain explicit remote requests."""
import argparse
from collections import OrderedDict
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shlex
import subprocess
import threading
import time
from urllib.parse import urlparse, parse_qs, quote
from urllib.request import Request, urlopen
from urllib.error import HTTPError


class Mirror:
    def __init__(self, config):
        self.config = config
        self.root = Path(config['cache'])
        self.root.mkdir(parents=True, exist_ok=True)
        self.limit = config.get('limit_bytes', 4*1024**2)
        self.lock = threading.RLock()
        self.entries = OrderedDict()
        self.requested = OrderedDict()
        self.seen = set()
        self.remote = {}
        self.last_sync = None
        self.error = None
        self.wake = threading.Event()
        self.frames = json.loads((self.root/'frames.json').read_text()) if (self.root/'frames.json').exists() else {}
        self.metadata = json.loads((self.root/'metadata.json').read_text()) if (self.root/'metadata.json').exists() else {}
        p = self.root/'index.json'
        if p.exists():
            old = json.loads(p.read_text())
            if old['origin_run'] != config['remote_cache']:
                raise ValueError('Local cache belongs to another run')
            for name, entry in old['entries'].items():
                if self.path(name).is_file():
                    self.entries[name] = entry
        self.evict(0)

    def path(self, name):
        return self.root/(hashlib.sha256(name.encode()).hexdigest()+'.webp')

    def used(self):
        return sum(p.stat().st_blocks*512 for p in self.root.iterdir() if p.is_file())

    def evict(self, incoming):
        while self.entries and self.used()+incoming > self.limit-512*1024:
            # Keep the opening frame even after the replay exceeds the local quota.
            first = next(iter(self.frames.get('frames', [])), None)
            name = next((n for n in self.entries if n != first), None)
            if name is None:break
            del self.entries[name]
            self.path(name).unlink(missing_ok=True)
        if self.used()+incoming > self.limit-512*1024:
            raise ValueError('Local preview quota exceeded')

    def save(self):
        p=self.root/'index.json';tmp=p.with_suffix('.tmp')
        tmp.write_text(json.dumps(dict(origin_run=self.config['remote_cache'],entries=self.entries)))
        tmp.replace(p)

    def put(self, name, body, token):
        if not body.startswith(b'RIFF') or body[8:12] != b'WEBP':
            raise ValueError('Remote response is not WebP')
        with self.lock:
            self.evict(((len(body)+4095)//4096)*4096)
            p=self.path(name);tmp=p.with_suffix('.tmp');tmp.write_bytes(body);tmp.replace(p)
            self.entries[name] = dict(token=token,bytes=len(body))
            self.entries.move_to_end(name)
            self.save()

    def request(self, name, priority=True):
        with self.lock:
            if name not in self.frames.get('frames',[]):
                raise ValueError('Unknown frame')
            if name in self.entries:
                self.entries.move_to_end(name)
                return self.path(name).read_bytes()
            if len(self.requested)<8:
                self.requested[name] = priority
                if priority:self.requested.move_to_end(name,last=False)
            self.wake.set()
        return None

    def status(self):
        with self.lock:
            return dict(state='local',ready=len(self.entries),total=len(self.frames.get('frames',[])),
                        ready_names=list(self.entries),bytes=self.used(),limit_bytes=self.limit,
                        remote_ready=self.remote.get('ready'),remote_state=self.remote.get('state'),
                        remote_reason=self.remote.get('reason'),last_sync=self.last_sync,error=self.error)

    def fetch(self, path):
        with urlopen(self.config['upstream']+path,timeout=8) as r:
            return r.status,r.read()

    def sync(self):
        # Cached metadata is always served locally; a slow SSH read is background work.
        if not self.frames or not self.metadata:
            for name in ('frames','metadata'):
                code,body=self.fetch('/api/'+name)
                data=json.loads(body)
                with self.lock:
                    setattr(self,name,data)
                    (self.root/(name+'.json')).write_bytes(body)
        remote_path=self.config['remote_cache']
        script="import json,pathlib; p=pathlib.Path("+repr(remote_path)+"); print(json.dumps({'index':json.loads((p/'index.json').read_text()),'state':json.loads((p/'worker.json').read_text())}))"
        command='python3 -B -c '+shlex.quote(script)
        raw=subprocess.check_output(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5',self.config['host'],command],timeout=12)
        remote=json.loads(raw);index=remote['index']
        with self.lock:
            self.remote=remote['state']
            requested=list(self.requested)
            self.requested.clear()
            for name,entry in list(self.entries.items()):
                source=index['entries'].get(name)
                # Remote LRU eviction alone does not invalidate an immutable local frame.
                if source and entry['token'] != [index['identity'],source['source']]:
                    self.path(name).unlink(missing_ok=True);del self.entries[name]
                elif entry['token'][0] != index['identity']:
                    self.path(name).unlink(missing_ok=True);del self.entries[name]
        candidates=requested+[n for n in index['entries'] if n not in requested]
        for name in candidates:
            source=index['entries'].get(name)
            if source:
                token=[index['identity'],source['source']]
                key=(name,json.dumps(token))
                with self.lock:
                    if name in self.entries or (key in self.seen and name not in requested):continue
                code,body=self.fetch('/api/preview?name='+quote(name))
                if code==200 and len(body)==source['bytes']:
                    self.put(name,body,token);self.seen.add(key)
            elif name in requested:
                # Queue an explicit requested frame, without making the browser wait for it.
                self.fetch('/api/preview?name='+quote(name))
        with self.lock:
            self.last_sync=time.time();self.error=None;self.save()

    def work(self):
        while True:
            try:self.sync()
            except Exception as error:
                with self.lock:self.error=str(error)
            self.wake.wait(30);self.wake.clear()
            time.sleep(1)


def handler(mirror):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass

        def send(self, body, content_type='application/json', code=200, extra=None):
            self.send_response(code);self.send_header('Content-Type',content_type)
            self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store')
            for k,v in (extra or {}).items():self.send_header(k,v)
            self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):pass

        def proxy(self):
            body=self.rfile.read(int(self.headers.get('Content-Length','0'))) if self.command=='POST' else None
            request=Request(mirror.config['upstream']+self.path,data=body,method=self.command)
            if self.headers.get('Content-Type'):request.add_header('Content-Type',self.headers['Content-Type'])
            try:r=urlopen(request,timeout=120)
            except HTTPError as error:r=error
            with r:
                data=r.read()
                self.send(data,r.headers.get('Content-Type','application/octet-stream'),r.status,
                          {'Content-Encoding':r.headers['Content-Encoding']} if r.headers.get('Content-Encoding') else {})

        def do_POST(self):
            try:self.proxy()
            except Exception as error:self.send(json.dumps({'error':str(error)}).encode(),code=502)

        def do_GET(self):
            url=urlparse(self.path)
            try:
                if url.path in ('/','/group_tracking.js'):
                    p=Path(__file__).with_name('index.html' if url.path=='/' else 'group_tracking.js')
                    self.send(p.read_bytes(),'text/html; charset=utf-8' if url.path=='/' else 'text/javascript; charset=utf-8')
                elif url.path=='/api/frames':
                    with mirror.lock:data=dict(mirror.frames,compact_preview=mirror.status(),preview_ready=True)
                    self.send(json.dumps(data).encode())
                elif url.path=='/api/metadata':self.send(json.dumps(mirror.metadata).encode())
                elif url.path=='/api/preview-status':self.send(json.dumps(mirror.status()).encode())
                elif url.path=='/api/preview':
                    q=parse_qs(url.query);body=mirror.request(q.get('name',[''])[0],q.get('prefetch',['0'])[0]!='1')
                    if body is None:self.send(json.dumps(mirror.status()).encode(),code=202)
                    else:self.send(body,'image/webp')
                else:self.proxy()
            except ValueError as error:self.send(json.dumps({'error':str(error)}).encode(),code=400)
            except Exception as error:self.send(json.dumps({'error':str(error)}).encode(),code=502)
    return Handler


def main():
    p=argparse.ArgumentParser();p.add_argument('--config',type=Path,required=True);p.add_argument('--seed-only',action='store_true')
    args=p.parse_args();mirrors=[]
    for config in json.loads(args.config.read_text())['viewers']:
        mirror=Mirror(config);mirrors.append(mirror)
        if args.seed_only:mirror.sync();print(json.dumps(mirror.status()),flush=True)
    if args.seed_only:return
    servers=[]
    for mirror in mirrors:
        server=ThreadingHTTPServer(('127.0.0.1',mirror.config['port']),handler(mirror));servers.append(server)
        threading.Thread(target=mirror.work,daemon=True).start()
        threading.Thread(target=server.serve_forever,daemon=True).start()
        print('LOCAL PLAYBACK',mirror.config['port'],mirror.status(),flush=True)
    threading.Event().wait()


if __name__=='__main__':main()
