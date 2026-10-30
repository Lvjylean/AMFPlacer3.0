#!/usr/bin/env python3
"""Read-only AMF coordinate trace viewer; Pillow for bounded preview rendering."""
import argparse
import os
import sys
from viewer_session import PageLeases, LeaseHandler, serve
import gzip
import json
import math
import re
import zipfile
import subprocess
import threading
import struct
from collections import OrderedDict
from trace_stages import trace_stages
from group_tracking import GroupTracker
from compact_preview import CompactPreview
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs


def frames(directory):
    def key(p):
        return ('Packing' in p.name, [int(x) if x.isdigit() else x for x in re.split(r'(\d+)', p.name)])
    return sorted(directory.glob('DumpAllCoordTrace-*.gz'), key=key)


def read_frame(path):
    points = []
    with gzip.open(path, 'rt') as source:
        for line in source:
            fields = line.split(maxsplit=2)
            if not fields:
                continue
            x, y = map(float, fields[:2])
            if not (math.isfinite(x) and math.isfinite(y)):
                raise ValueError('Non-finite coordinate')
            points.append([x, y, fields[2].strip() if len(fields) > 2 else ''])
    return points


def resource_type(t):
    if t.startswith('LUT'): return 'LUT'
    if t.startswith(('FD', 'LD')): return 'FF'
    if t.startswith('DSP'): return 'DSP'
    if t.startswith(('RAMB', 'FIFO18', 'FIFO36')): return 'BRAM'
    if t.startswith('URAM'): return 'URAM'
    if t.startswith('CARRY'): return 'CARRY'
    if t.startswith('MUX'): return 'MUX'
    if t.startswith(('RAM', 'SRL')): return 'LUTRAM/SRL'
    return 'OTHER'


def load_metadata(config_path):
    if not config_path: return {}, {}
    config = json.loads(config_path.read_text())
    types = {}
    with zipfile.ZipFile(config['vivado extracted design information file']) as z:
        with z.open('allCellPinNet') as f:
            for raw in f:
                if raw.startswith(b'curCell=> '):
                    name, typ = raw.decode().strip()[10:].rsplit(' type=> ', 1)
                    types[name] = resource_type(typ)
    virtual_path = config_path.parent / 'visualization-virtual-cells.json'
    if virtual_path.exists():
        virtual_names = json.loads(virtual_path.read_text())
        if any(name in types for name in virtual_names):
            raise ValueError('Virtual-cell override overlaps original netlist')
        types.update({name: 'VIRTUAL' for name in virtual_names})
    groups = {}
    with zipfile.ZipFile(config['vivado extracted device information file']) as z:
        with z.open('exportSiteLocation') as f:
            for raw in f:
                line = raw.decode()
                fields = dict(re.findall(r'(\w+)=> (\S+)', line))
                cr = fields['clockRegionName']
                if not re.fullmatch(r'X\d+Y\d+', cr): continue
                x, y = float(fields['centerx']), float(fields['centery'])
                g = groups.setdefault(cr, [x, x, y, y])
                g[0], g[1] = min(g[0], x), max(g[1], x)
                if fields['site'].startswith('SLICE'):
                    g[2], g[3] = min(g[2], y), max(g[3], y)
    physical = json.loads((Path(config['physical boundary model file']).parent/'boundaries.json').read_text())
    bounds = physical['coordinate_bounds']
    nx, ny = int(config['clockRegionXNum']), int(config['clockRegionYNum'])
    xb = [bounds[0]] + [(groups['X%dY0'%(i-1)][1]+groups['X%dY0'%i][0])/2 for i in range(1,nx)] + [bounds[1]]
    yb = [bounds[2]] + [(groups['X0Y%d'%(i-1)][3]+groups['X0Y%d'%i][2])/2 for i in range(1,ny)] + [bounds[3]]
    assert len(groups) == nx*ny
    return types, dict(bounds=bounds, cr_x=xb, cr_y=yb,
        slr=[b['coordinate'] for b in physical['boundaries'] if b['kind']=='SLR'],
        counts=dict(Counter(types.values())), source=str(config_path),
        cr_method='Site-coordinate adjacent-region midpoints; physical model outer bounds')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--traces', type=Path, required=True)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--preview-cache', type=Path)
    parser.add_argument('--preview-limit-mib', type=int, default=32)
    parser.add_argument('--metadata-cache', type=Path)
    parser.add_argument('--preview-cpu', type=float, default=.25)
    parser.add_argument('--preview-read-mib-s', type=float, default=2)
    parser.add_argument('--preview-progressive', action='store_true')
    args = parser.parse_args()
    directory = args.traces.resolve()
    if not directory.is_dir():
        parser.error('Trace directory does not exist')

    if args.metadata_cache:
        from metadata_snapshot import load
        types, metadata = load(args.metadata_cache, args.config)
    else:
        types, metadata = load_metadata(args.config)
    log_path = args.config.parent / "logs/amf.log" if args.config else directory.parent / "logs/amf.log"
    metadata["stages"] = trace_stages(log_path)
    preview = CompactPreview(frames(directory), types, metadata, args.preview_cache, args.config,
                             limit_bytes=args.preview_limit_mib*1024**2, cpu_fraction=args.preview_cpu,
                             read_mib_s=args.preview_read_mib_s,progressive=args.preview_progressive) if args.preview_cache else None
    lifecycle=PageLeases(token=os.environ.get('AMF_VIEWER_OWNER_TOKEN'))
    live_lock = threading.Lock()
    live_ids = None
    live_dictionary = None
    live_frames = OrderedDict()
    def live_payload(name=None):
        nonlocal live_ids, live_dictionary
        with live_lock:
            if live_ids is None:
                names = list(types)
                seen = set(names)
                for point in read_frame(frames(directory)[0]):
                    if point[2] not in seen:
                        names.append(point[2]); seen.add(point[2])
                live_ids = {name:i for i,name in enumerate(names)}
                live_dictionary = gzip.compress(json.dumps({'names':names,'types':[types.get(n,'UNKNOWN') if n else 'VIRTUAL' for n in names]},separators=(',',':')).encode(),compresslevel=3)
            if name is None:return live_dictionary
            if name not in live_frames:
                data = bytearray()
                with gzip.open(directory/name,'rt') as f:
                    for line in f:
                        parts=line.split(maxsplit=2)
                        if not parts:continue
                        cell=parts[2].strip() if len(parts)>2 else ''
                        if cell not in live_ids:raise ValueError('Live dictionary missing cell; use raw frame')
                        data.extend(struct.pack('<Iff',live_ids[cell],float(parts[0]),float(parts[1])))
                live_frames[name]=gzip.compress(data,compresslevel=3)
                while len(live_frames)>3:live_frames.popitem(last=False)
            live_frames.move_to_end(name)
            return live_frames[name]
    tracker = GroupTracker(directory)
    exports = {}
    export_lock = threading.Lock()
    export_dir = directory.parent/"viewer-exports"

    class Handler(BaseHTTPRequestHandler):
        def log_request(self, code='-', size='-'):
            # Replaying frames must not grow access logs on disk.
            if str(code).startswith(('4', '5')):
                super().log_request(code, size)

        def export_state(self, speed):
            target=export_dir/('placement-stages-v1-'+speed+'x.gif')
            proc=exports.get(speed)
            if proc is not None and proc.poll() is None:
                return {'state':'running'}
            if proc is not None and proc.returncode != 0:
                return {'state':'failed','message':'GIF export failed; retry available'}
            if target.exists():return {'state':'ready','url':'/api/gif?speed='+speed,'bytes':target.stat().st_size}
            return {'state':'idle'}

        def group_json(self, data, status=200):
            body=json.dumps(data,ensure_ascii=False).encode()
            self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)

        def do_POST(self):
            if urlparse(self.path).path == '/api/group-select':
                try:
                    length=int(self.headers.get('Content-Length','0'))
                    if not 0 < length <= 16*1024*1024:raise ValueError('请求大小必须在 1 字节至 16 MiB 之间')
                    self.group_json(tracker.select(json.loads(self.rfile.read(length))))
                except (ValueError, OSError, KeyError) as error:self.group_json({'error':str(error)},400)
                return
            url=urlparse(self.path);speed=parse_qs(url.query).get('speed',['2'])[0]
            if url.path!='/api/gif' or speed not in ('0.5','1','1.5','2','5'):
                self.send_error(400);return
            with export_lock:
                state=self.export_state(speed)
                if state['state'] in ('idle','failed'):
                    if any(p.poll() is None for p in exports.values()):
                        state={'state':'busy','message':'Another GIF is being generated; retry shortly'}
                    else:
                        export_dir.mkdir(exist_ok=True)
                        exports[speed]=subprocess.Popen(['/usr/bin/python3',str(Path(__file__).with_name('export_gif.py')),'--run',str(directory.parent),'--speed',speed,'--output',str(export_dir/('placement-stages-v1-'+speed+'x.gif'))],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                        state={'state':'running'}
            body=json.dumps(state).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

        def do_GET(self):
            url = urlparse(self.path)
            try:
                if url.path == '/group_tracking.js':
                    body=Path(__file__).with_name('group_tracking.js').read_bytes();content_type='text/javascript; charset=utf-8'
                elif url.path == '/api/group-modules':
                    try:self.group_json(tracker.suggestions(parse_qs(url.query).get('query',[''])[0]))
                    except (ValueError,OSError) as error:self.group_json({'error':str(error)},409)
                    return
                elif url.path == '/api/group-frame':
                    q=parse_qs(url.query)
                    try:body=tracker.frame(q.get('token',[''])[0],q.get('name',[''])[0])
                    except (ValueError,OSError) as error:self.group_json({'error':str(error)},409);return
                    self.send_response(200);self.send_header('Content-Encoding','gzip');self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body);return
                elif url.path in ('/api/gif','/api/gif/status'):
                    speed=parse_qs(url.query).get('speed',['2'])[0]
                    if speed not in ('0.5','1','1.5','2','5'):
                        self.send_error(400);return
                    state=self.export_state(speed)
                    if url.path.endswith('/status'):
                        body=json.dumps(state).encode();content_type='application/json'
                    elif state['state']=='ready':
                        body=(export_dir/('placement-stages-v1-'+speed+'x.gif')).read_bytes();content_type='image/gif'
                    else:
                        self.send_error(409,'GIF not ready');return
                elif url.path == '/':
                    body = lifecycle.inject(Path(__file__).with_name('index.html').read_bytes())
                    content_type = 'text/html; charset=utf-8'
                elif url.path == '/api/metadata':
                    metadata['stages'] = trace_stages(log_path)
                    body = json.dumps(metadata).encode()
                    content_type = 'application/json'
                elif url.path == '/api/frames':
                    body = json.dumps({'directory': str(directory), 'frames': [p.name for p in frames(directory)], 'preview_ready': preview is not None or (directory/'.viewer-preview-v1/manifest.json').exists(), 'compact_preview': preview.status() if preview else None, 'live_binary': True, 'binary_ready': (directory/'.viewer-binary-v1/manifest.json').exists(), 'stages': trace_stages(log_path)}).encode()
                    content_type = 'application/json'
                elif url.path == '/api/preview-manifest':
                    body=json.dumps(preview.manifest() if preview else {}).encode();content_type='application/json'
                elif url.path == '/api/preview-status':
                    body=json.dumps(preview.status() if preview else {'state':'disabled'}).encode();content_type='application/json'
                elif url.path == '/api/preview':
                    name=parse_qs(url.query).get('name',[''])[0]
                    if name not in {p.name for p in frames(directory)}:
                        self.send_error(404);return
                    if preview:
                        body=preview.request(name, priority=parse_qs(url.query).get('prefetch',['0'])[0]!='1')
                        if body is None:
                            state=preview.status(); body=json.dumps(state).encode()
                            self.send_response(503 if state['state']=='failed' else 202)
                            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)))
                            self.send_header('Cache-Control','no-store');self.send_header('Retry-After','2')
                            self.end_headers();self.wfile.write(body);return
                        self.send_response(200);self.send_header('Content-Type','image/webp')
                        self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store')
                        self.end_headers();self.wfile.write(body);return
                    target=directory/'.viewer-preview-v1'/(name+'.webp')
                    if not target.exists():self.send_error(409,'Preview not ready');return
                    body=target.read_bytes();content_type='image/webp'
                elif url.path in ('/api/live-binary','/api/live-dictionary'):
                    name=parse_qs(url.query).get('name',[''])[0]
                    if url.path=='/api/live-binary' and name not in {p.name for p in frames(directory)}:
                        self.send_error(404);return
                    try:body=live_payload(name if url.path=='/api/live-binary' else None)
                    except ValueError:
                        self.send_error(409,'Use raw frame');return
                    self.send_response(200)
                    self.send_header('Content-Encoding','gzip')
                    self.send_header('Content-Type','application/json' if url.path=='/api/live-dictionary' else 'application/octet-stream')
                    self.send_header('Content-Length',str(len(body)))
                    self.send_header('Cache-Control','no-store')
                    self.end_headers();self.wfile.write(body);return
                elif url.path in ('/api/binary', '/api/dictionary'):
                    cache = directory / '.viewer-binary-v1'
                    if not (cache/'manifest.json').exists():
                        self.send_error(409, 'Binary cache is not ready')
                        return
                    name = parse_qs(url.query).get('name', [''])[0]
                    if url.path == '/api/dictionary':
                        target = cache/'dictionary.json.gz'
                        content_type = 'application/json'
                    else:
                        if name not in {p.name for p in frames(directory)}:
                            self.send_error(404)
                            return
                        target = cache/(name+'.bin.gz')
                        content_type = 'application/octet-stream'
                    body = target.read_bytes()
                    self.send_response(200)
                    self.send_header('Content-Encoding', 'gzip')
                    self.send_header('Content-Type', content_type)
                    self.send_header('Content-Length', str(len(body)))
                    self.send_header('Cache-Control', 'no-store')
                    self.end_headers()
                    self.wfile.write(body)
                    return
                elif url.path == '/api/frame':
                    name = parse_qs(url.query).get('name', [''])[0]
                    allowed = {p.name: p for p in frames(directory)}
                    if name not in allowed:
                        self.send_error(404)
                        return
                    points = read_frame(allowed[name])
                    for point in points:
                        point.append(types.get(point[2], 'UNKNOWN') if point[2] else 'VIRTUAL')
                    body = json.dumps({'name': name, 'points': points}, separators=(',', ':'), allow_nan=False).encode()
                    content_type = 'application/json'
                else:
                    self.send_error(404)
                    return
                compressed = content_type == 'application/json' and len(body) > 65536 and 'gzip' in self.headers.get('Accept-Encoding', '')
                if compressed:
                    body = gzip.compress(body, compresslevel=1)
                self.send_response(200)
                if compressed:
                    self.send_header('Content-Encoding', 'gzip')
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                self.wfile.write(body)
            except (OSError, ValueError, EOFError) as error:
                self.send_error(409, 'Trace is incomplete or invalid; retry after writing completes')
                print(type(error).__name__, str(error), flush=True)

    print('AMF viewer http://127.0.0.1:%d traces=%s' % (args.port, directory), flush=True)
    class SessionHandler(LeaseHandler,Handler):pass
    SessionHandler.lifecycle=lifecycle
    if os.environ.get('AMF_VIEWER_WATCH_STDIN')=='1':
        def watch_owner():
            for _ in sys.stdin:pass
            lifecycle.stop.set()
        threading.Thread(target=watch_owner,daemon=True).start()
    try:serve(ThreadingHTTPServer(('127.0.0.1', args.port),SessionHandler),lifecycle)
    finally:
        if preview:preview.stop.set();preview.thread.join()



if __name__ == '__main__':
    main()
