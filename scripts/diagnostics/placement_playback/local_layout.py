"""Local SLL/Pblock extension of the existing playback UI and history picker."""
import argparse
import fcntl
import gzip
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
from urllib.parse import parse_qs, urlsplit
from urllib.request import urlopen

ROOT=Path(__file__).resolve().parent
PROJECT=ROOT.parents[2]
PICKER=PROJECT/'experiments/preflight/20261020-viewer-launcher-compat/mac'
sys.path.insert(0,str(PICKER))
from viewer_session import PageLeases, LeaseHandler, serve


class Bundle:
    def __init__(self,path):
        self.root=Path(path).resolve()
        self.manifest=json.loads((self.root/'manifest.json').read_text())
        if self.manifest.get('schema')!='amf-local-layout-v1': raise ValueError('Unknown layout schema')
        self.frames={f['name']:f for f in self.manifest['frames']}
        if not self.frames or len(self.frames)!=len(self.manifest['frames']): raise ValueError('Invalid frame identities')
        for frame in self.frames.values():
            for key in ('binary','dictionary','layers'):
                if key in frame: self.file(frame[key])

    def file(self,name):
        p=(self.root/name).resolve()
        if self.root not in p.parents or not p.is_file(): raise ValueError('Invalid bundle asset')
        return p

    def frame(self,name):
        if name not in self.frames: raise ValueError('Unknown frame')
        return self.frames[name]

    def layers(self,name):
        frame=self.frame(name)
        if 'layers' not in frame:
            return json.dumps(dict(state='missing',crossings=[],tiles=[])).encode(),False
        p=self.file(frame['layers']);return p.read_bytes(),p.suffix=='.gz'


def handler(bundle,pool):
    m=bundle.manifest
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass

        def send(self,body,kind='application/json',code=200,zipped=False):
            if not isinstance(body,bytes): body=json.dumps(body,ensure_ascii=False).encode()
            self.send_response(code);self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store')
            if zipped:self.send_header('Content-Encoding','gzip')
            self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):pass

        def do_GET(self):
            u=urlsplit(self.path);q=parse_qs(u.query)
            name=q.get('name',[next(iter(bundle.frames))])[0]
            try:
                if u.path=='/':
                    self.send(pool.inject((ROOT/'layered_view.html').read_bytes()),'text/html; charset=utf-8')
                elif u.path in ('/layout_layers.js','/group_tracking.js'):
                    self.send((ROOT/u.path[1:]).read_bytes(),'text/javascript; charset=utf-8')
                elif u.path=='/api/viewer-identity':
                    self.send(dict(kind='amf-local-layout',version=1,bundle=str(bundle.root),run=m['run']))
                elif u.path=='/api/metadata':
                    self.send(dict(m['metadata'],title=m['title'],pblocks=m.get('pblocks',[]),
                                   active_region=m.get('active_region'),mode=m['mode'],
                                   history_note=m.get('history_note',''),placement_stage=m['placement_stage'],
                                   static_context=m.get('static_context'),source=m['run']))
                elif u.path=='/api/frames':
                    self.send(dict(directory=m['run'],frames=list(bundle.frames),binary_ready=True,
                                   preview_ready=False,layout_layers=True,
                                   stages={f['name']:dict(label=f.get('label','AMF 最终布局快照'),
                                                         elapsed_seconds=f.get('elapsed_seconds','未记录')) for f in m['frames']}))
                elif u.path in ('/api/binary','/api/dictionary'):
                    f=bundle.frame(name);p=bundle.file(f['binary' if u.path=='/api/binary' else 'dictionary'])
                    self.send(p.read_bytes(),'application/octet-stream' if u.path=='/api/binary' else 'application/json',zipped=p.suffix=='.gz')
                elif u.path=='/api/layers':
                    body,zipped=bundle.layers(name);self.send(body,zipped=zipped)
                elif u.path=='/api/preview-status':self.send({})
                else:self.send({'error':'Unknown endpoint'},code=404)
            except (OSError,ValueError,KeyError) as e:self.send({'error':str(e)},code=400)

    class SessionHandler(LeaseHandler,Handler):pass
    SessionHandler.lifecycle=pool
    return SessionHandler


def identity(port):
    try:
        with urlopen(f'http://127.0.0.1:{port}/api/viewer-identity',timeout=1) as r:return json.load(r)
    except (OSError,ValueError):return {}


def ensure(path):
    bundle=Bundle(path)
    state=PROJECT/'experiments/preflight/20261030-local-viewer-layers/state'
    state.mkdir(parents=True,exist_ok=True)
    with (state/'ensure.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        free=None
        for port in range(18910,18940):
            with socket.socket() as s:
                used=s.connect_ex(('127.0.0.1',port))==0
            if used:
                d=identity(port)
                if d.get('kind')=='amf-local-layout' and d.get('bundle')==str(bundle.root):
                    return f'http://127.0.0.1:{port}/'
            elif free is None:free=port
        if free is None:raise RuntimeError('No free local layout port')
        with (state/f'viewer-{free}.log').open('ab') as log:
            p=subprocess.Popen([sys.executable,'-B',str(Path(__file__).resolve()),'--bundle',str(bundle.root),'--port',str(free)],
                               stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        for _ in range(60):
            d=identity(free)
            if d.get('kind')=='amf-local-layout' and d.get('bundle')==str(bundle.root):return f'http://127.0.0.1:{free}/'
            if p.poll() is not None:raise RuntimeError('Layout viewer exited; see local log')
            time.sleep(.1)
        p.terminate();raise RuntimeError('Layout viewer startup timed out')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--bundle',required=True);p.add_argument('--port',type=int,default=18910)
    p.add_argument('--ensure',action='store_true');a=p.parse_args()
    if a.ensure:print(ensure(a.bundle))
    else:
        pool=PageLeases()
        for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:pool.stop.set())
        serve(ThreadingHTTPServer(('127.0.0.1',a.port),handler(Bundle(a.bundle),pool)),pool)
