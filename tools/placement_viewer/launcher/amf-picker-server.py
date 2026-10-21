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
LOCK = threading.Lock()
CACHE = {}


def matching_service(url, selected):
    """A port is reusable only while it serves the selected trace directory."""
    try:
        parsed = urlsplit(url)
        if (parsed.scheme != 'http' or parsed.hostname != '127.0.0.1'
                or not parsed.port or parsed.username or parsed.password
                or parsed.path not in ('', '/') or parsed.query or parsed.fragment):
            return None
        with urlopen(url.rstrip('/') + '/api/frames', timeout=3) as response:
            data = json.load(response)
        expected = selected.get('directory')
        allowed = {expected} if expected else {
            selected['run'].rstrip('/') + '/placement',
            selected['run'].rstrip('/') + '/placement/visualization'}
        if data.get('directory') in allowed:
            return data
    except (OSError, ValueError, TypeError):
        pass
    return None


def open_selected(selected):
    if selected.get('classification_warning'):
        raise ValueError('Device classification requires review')
    if 'frames' in selected and not selected['frames']:
        raise ValueError('This checkpoint has no AMF trajectory')
    chosen = selected['run']
    cached = CACHE.get(chosen)
    data = matching_service(cached, selected) if cached else None
    if data is None:
        CACHE.pop(chosen, None)
        command = [sys.executable, str(ROOT / 'open-amf-viewer-mac.py'), '--run', chosen]
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
    from viewer_settings import load_settings
    ThreadingHTTPServer(('127.0.0.1', load_settings()['picker_port']), Handler).serve_forever()
