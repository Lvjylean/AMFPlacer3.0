"""Serve immutable local preview images using the existing AMF playback UI."""
import argparse
import fcntl
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

PROJECT = Path(__file__).resolve().parents[3]
PICKER = PROJECT/'experiments/preflight/20261020-viewer-launcher-compat/mac'
UI = PROJECT/'experiments/preflight/20261024-viewer-session-lifecycle/playback'
REGISTRY = PICKER/'amf-offline-previews.json'
sys.path.insert(0, str(PICKER))
from viewer_session import PageLeases, LeaseHandler, serve


def read_identity(port):
    try:
        with urlopen(f'http://127.0.0.1:{port}/api/viewer-identity',timeout=2) as response:
            return json.load(response)
    except (OSError,ValueError):
        return {}


def matches(view):
    identity=read_identity(view['port'])
    return (identity.get('kind')=='amf-compact-offline'
            and identity.get('config_source')==view['run']+'/config.json'
            and identity.get('directory')==view['directory'])


def occupied(port):
    with socket.socket() as sock:
        sock.settimeout(.2)
        return sock.connect_ex(('127.0.0.1',port))==0


def ensure(run):
    view=next(v for v in json.loads(REGISTRY.read_text())['viewers'] if v['run']==run)
    state=REGISTRY.parent/'offline-preview-state'
    state.mkdir(exist_ok=True)
    with (state/'ensure.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if matches(view):return f"http://127.0.0.1:{view['port']}/"
        if occupied(view['port']):raise RuntimeError('Local preview port belongs to another service')
        with (state/(view['key']+'.log')).open('ab') as log:
            process=subprocess.Popen([sys.executable,'-B',str(Path(__file__).resolve()),'--serve',view['key']],
                stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        for _ in range(100):
            if matches(view):return f"http://127.0.0.1:{view['port']}/"
            if process.poll() is not None:raise RuntimeError('Local preview startup failed; see log')
            time.sleep(.1)
        process.terminate()
        raise RuntimeError('Local preview startup timed out')


def page(view, manifest):
    html=(UI/'index.html').read_text()
    html=html.replace('AMF3 / 布局优化过程', view['name'])
    html=html.replace('<option value="detail">完整坐标 · 单 cell 检查</option>', '')
    html=html.replace('已缓存帧 · 快速概览','本地抽样帧').replace('全部帧 · 逐帧','全部抽样帧')
    html=html.replace('支持缩放拖动；查看单 cell 请切完整坐标模式',
        f"本地离线 · 抽样 {manifest['sampled_frames']} / {manifest['source_frames']} 帧 · 可缩放、拖动和播放")
    html=html.replace('单 cell 坐标请切换完整坐标模式','')
    start=html.index('<footer>');end=html.index('</footer>',start)+len('</footer>')
    html=html[:start]+('<footer>本地粗略回放：按时间抽帧，保留阶段起止与最终布局；每张图投影全部真实 cells，重叠点会合并。'
        '颜色：蓝 LUT · 绿 FF · 橙 DSP · 紫 BRAM · 粉 URAM · 黄 CARRY。黄色横线为 SLR 接缝。'
        '这是 AMF 布局轨迹，不是 DCP 布线图；此副本不含单 cell 查询或模块追踪。</footer>')+html[end:]
    html=html.replace('</style>', '#groupPanel,#activeGroupBadge,#search,#cellDetails,#exportGif,#exportStatus,#legend{display:none!important}</style>',1)
    return html.encode()


def run_server(key):
    view=next(v for v in json.loads(REGISTRY.read_text())['viewers'] if v['key']==key)
    root=Path(view['cache'])
    manifest=json.loads((root/'manifest.json').read_text())
    metadata=json.loads((root/'metadata.json').read_text())
    frames=json.loads((root/'frames.json').read_text())
    assert manifest['run']==view['run'] and frames['directory']==view['directory']
    assert metadata['source']==view['run']+'/config.json'
    assert len(frames['frames'])==len(manifest['entries'])==manifest['sampled_frames']
    lifecycle=PageLeases()
    status=dict(state='local',ready=len(frames['frames']),total=len(frames['frames']),
        ready_names=frames['frames'],bytes=sum(e['bytes'] for e in manifest['entries'].values()),
        limit_bytes=64*1024**2,error=None,last_sync=manifest['created_at'],remote_state='offline')

    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass

        def send(self,data,kind='application/json',code=200):
            body=data if isinstance(data,bytes) else json.dumps(data,ensure_ascii=False).encode()
            self.send_response(code);self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store')
            self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError):pass

        def do_GET(self):
            parsed=urlsplit(self.path)
            if parsed.path=='/':self.send(lifecycle.inject(page(view,manifest)),'text/html; charset=utf-8')
            elif parsed.path=='/group_tracking.js':self.send((UI/'group_tracking.js').read_bytes(),'text/javascript; charset=utf-8')
            elif parsed.path=='/api/metadata':self.send(metadata)
            elif parsed.path=='/api/frames':self.send(dict(frames,compact_preview=status))
            elif parsed.path=='/api/preview-status':self.send(status)
            elif parsed.path=='/api/viewer-identity':
                self.send(dict(version=1,kind='amf-compact-offline',directory=frames['directory'],
                    config_source=metadata['source'],capabilities=['compact_preview','offline'],lifetime='page-leased'))
            elif parsed.path=='/api/preview':
                name=parse_qs(parsed.query).get('name',[''])[0]
                entry=manifest['entries'].get(name)
                if entry is None:return self.send({'error':'Unknown sampled frame'},code=404)
                target=(root/entry['file']).resolve()
                if root.resolve() not in target.parents:return self.send({'error':'Invalid image path'},code=400)
                self.send(target.read_bytes(),'image/webp')
            else:self.send({'error':'本地粗略版只提供已同步的预览图'},code=404)

        def do_POST(self):self.send({'error':'本地粗略版不提供完整坐标查询'},code=404)

    class SessionHandler(LeaseHandler,Handler):pass
    SessionHandler.lifecycle=lifecycle
    for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:lifecycle.stop.set())
    serve(ThreadingHTTPServer(('127.0.0.1',view['port']),SessionHandler),lifecycle)


if __name__=='__main__':
    parser=argparse.ArgumentParser();group=parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--ensure');group.add_argument('--serve');args=parser.parse_args()
    if args.ensure:print(ensure(args.ensure))
    else:run_server(args.serve)
