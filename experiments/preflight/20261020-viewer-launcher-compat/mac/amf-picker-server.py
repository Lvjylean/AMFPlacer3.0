"""Local version picker: connect a placement only after explicit selection."""
import json
from pathlib import Path
import subprocess
import threading
import os
import runpy
import sys
from urllib.request import urlopen
from urllib.parse import urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent
from viewer_session import PageLeases, LeaseHandler, serve
LIFECYCLE = PageLeases()
LOCK = threading.Lock()
CACHE = {}


def matching_service(url, selected, kind='leased'):
    """A port is reusable only while it serves the selected trace directory."""
    try:
        parsed = urlsplit(url)
        if (parsed.scheme != 'http' or parsed.hostname != '127.0.0.1'
                or not parsed.port or parsed.username or parsed.password
                or parsed.path not in ('', '/') or parsed.query or parsed.fragment):
            return None
        if kind == 'leased':
            with urlopen(url.rstrip('/') + '/api/viewer-lifecycle', timeout=3) as response:
                if json.load(response).get('version') != 1:return None
        elif kind == 'amf-compact-local':
            with urlopen(url.rstrip('/') + '/api/viewer-identity', timeout=3) as response:
                identity = json.load(response)
            expected_source = selected['run'].rstrip('/') + '/config.json'
            if (identity.get('version') != 1 or identity.get('kind') != kind
                    or identity.get('config_source') != expected_source
                    or 'compact_preview' not in identity.get('capabilities', [])
                    or not {'full_coordinates','local_complete_cache'}.intersection(identity.get('capabilities', []))):
                return None
            with urlopen(url.rstrip('/') + '/api/metadata', timeout=3) as response:
                if json.load(response).get('source') != expected_source:return None
        else:
            return None
        with urlopen(url.rstrip('/') + '/api/frames', timeout=3) as response:
            data = json.load(response)
        expected = selected.get('directory')
        allowed = {expected} if expected else {
            selected['run'].rstrip('/') + '/placement',
            selected['run'].rstrip('/') + '/placement/visualization'}
        if kind == 'amf-compact-local' and (identity.get('directory') != data.get('directory')
                                           or not data.get('preview_ready')):
            return None
        if data.get('directory') in allowed:
            return data
    except (OSError, ValueError, TypeError):
        pass
    return None


def open_selected(selected):
    if selected.get('classification_warning'):
        raise ValueError('Device classification requires review')
    if selected.get('local_layout_bundle'):
        # A local extension uses exported coordinates only; no SSH or DCP open.
        project=ROOT.parents[3]
        extension=project/'scripts/diagnostics/placement_playback/local_layout.py'
        module=runpy.run_path(str(extension))
        bundle=module['Bundle'](selected['local_layout_bundle'])
        if bundle.manifest['run']!=selected['run'] or bundle.manifest['device']!=selected['device']:
            raise ValueError('Local layout identity does not match the selected record')
        url=module['ensure'](selected['local_layout_bundle'])
        return dict(url=url,directory=selected['run'],name=selected['name'],
                    frames=len(bundle.frames),identity_verified=True,local_layout=True)
    if 'frames' in selected and not selected['frames']:
        raise ValueError('This checkpoint has no AMF trajectory')
    chosen = selected['run']
    bindings_path = ROOT / 'amf-playback-bindings.json'
    if bindings_path.is_file():
        bindings = json.loads(bindings_path.read_text())
        if bindings.get('version') != 1:
            raise ValueError('Unsupported local playback bindings')
        binding = bindings.get('services', {}).get(chosen)
        if binding:
            if binding.get('kind') != 'amf-compact-local':
                raise ValueError('Unsupported local playback service')
            url = binding.get('url', '')
            starter=ROOT/'managed_compact.py'
            if starter.is_file():
                result=subprocess.run([sys.executable,'-B',str(starter),'--ensure',chosen],capture_output=True,text=True,timeout=120)
                if result.returncode:raise RuntimeError(result.stderr[-1200:] or '快速预览启动失败')
                if result.stdout.strip()!=url:raise RuntimeError('快速预览启动地址不匹配')
            data = matching_service(url, selected, kind='amf-compact-local')
            if data is None or data['directory'] != binding.get('directory'):
                raise RuntimeError('本轮快速预览服务尚未就绪或设计身份不匹配，请稍后重新连接。')
            return {'url': url, 'directory': data['directory'],
                    'name': selected.get('name', chosen), 'frames': len(data.get('frames', [])),
                    'identity_verified': True, 'default_mode': 'fast'}
    cached = CACHE.get(chosen)
    data = matching_service(cached, selected) if cached else None
    if data is None:
        CACHE.pop(chosen, None)
        if sys.platform.startswith('linux'):
            service = runpy.run_path(str(ROOT / 'amf-viewer-remote.py'))['start'](chosen)
            url = f"http://127.0.0.1:{service['port']}/"
        else:
            command = ([sys.executable, str(ROOT / 'open-amf-viewer-mac.py'), '--run', chosen]
                       if sys.platform == 'darwin' else
                       ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                        '-File', str(ROOT / 'open-amf-viewer.ps1'), '-RunPath', chosen, '-NoBrowser'])
            result = subprocess.run(command, capture_output=True,
                                    text=True, errors='replace', timeout=180)
            urls = [line.strip() for line in result.stdout.splitlines()
                    if line.startswith('http://127.0.0.1:')]
            if result.returncode or not urls:
                raise RuntimeError(result.stdout[-1500:] + result.stderr[-500:])
            url = urls[-1]
        data = matching_service(url, selected)
        if data is None:
            raise RuntimeError('查看器连接未通过设计身份校验；未打开其他设计，请重新连接。')
        CACHE[chosen] = url
    else:
        url = cached
    return {'url': url, 'directory': data['directory'],
            'name': selected.get('name', chosen), 'frames': len(data.get('frames', [])),
            'identity_verified': True}


class Handler(BaseHTTPRequestHandler):
    def send_data(self, data, content_type='application/json', status=200):
        body = data if isinstance(data, bytes) else json.dumps(data).encode()
        if content_type.startswith('text/html'):body=LIFECYCLE.inject(body)
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == '/api/health':
            self.send_data({'app': 'amf-lazy-picker', 'root': str(ROOT)})
        elif self.path == '/api/versions':
            self.send_data(json.loads((ROOT / 'amf-picker-data.json').read_text(encoding='utf-8-sig')))
        elif self.path == '/':
            self.send_data((ROOT / 'amf-picker.html').read_bytes(), 'text/html; charset=utf-8')
        else:
            self.send_data({'error': 'Not found'}, status=404)

    def do_POST(self):
        try:
            config = json.loads((ROOT / 'amf-picker-data.json').read_text(encoding='utf-8-sig'))
            if self.path not in ('/api/open', '/api/refresh') or self.headers.get('X-AMF-Token') != config['token']:
                self.send_data({'error': 'Invalid request'}, status=403)
                return
            if self.path == '/api/refresh':
                with LOCK:
                    runs = runpy.run_path(str(ROOT / 'open-amf-viewer-mac.py'))['remote']()
                    updated = dict(config, runs=runs)
                    target = ROOT / 'amf-picker-data.json'
                    temporary = target.with_suffix('.tmp')
                    temporary.write_text(json.dumps(updated), encoding='utf-8')
                    temporary.replace(target)
                    self.send_data(updated)
                return
            length = int(self.headers.get('Content-Length', '0'))
            if length < 1 or length > 4096:
                raise ValueError('Invalid request size')
            request = json.loads(self.rfile.read(length))
            chosen = request['run']
            selected = next((r for r in config['runs'] if r['run'] == chosen), None)
            if selected is None:
                raise ValueError('Unknown placement')
            if not request.get('device') or request['device'] != selected.get('device'):
                raise ValueError('Select the matching device before opening this design')
            # Serialize connections and revalidate identity on every cache hit.
            with LOCK:
                self.send_data(open_selected(selected))
        except Exception as error:
            self.send_data({'error': str(error)}, status=500)

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    class SessionHandler(LeaseHandler,Handler):pass
    SessionHandler.lifecycle=LIFECYCLE
    serve(ThreadingHTTPServer(('127.0.0.1',18760),SessionHandler),LIFECYCLE)
