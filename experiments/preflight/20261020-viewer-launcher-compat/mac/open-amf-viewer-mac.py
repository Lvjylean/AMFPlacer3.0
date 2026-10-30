#!/usr/bin/env python3
"""macOS launcher with on-demand SSH connections and personal credentials."""
import base64
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import time
from urllib.request import urlopen
import webbrowser
from viewer_hosts import host_for_run, remote_072, combined_catalog

ROOT = Path(__file__).resolve().parent
SERVER = 'jinyang@143.89.78.99'


def ssh_options():
    options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', '-o', 'IdentitiesOnly=yes']
    key = Path(os.environ.get('AMF_SSH_KEY', '~/.ssh/coldsun_ed25519')).expanduser()
    if key.is_file():
        options += ['-i', str(key)]
    elif os.environ.get('AMF_SSH_KEY'):
        raise RuntimeError(f'SSH key does not exist: {key}')
    return options


def remote_099(run=None):
    source = (ROOT / 'amf-viewer-remote.py').read_text()
    if run:
        source = 'import sys\nsys.argv=' + repr(['viewer', run]) + '\n' + source
    encoded = base64.b64encode(source.encode()).decode()
    result = subprocess.run(['ssh', *ssh_options(), SERVER,
                             f'echo {encoded} | base64 -d | python3'],
                            capture_output=True, text=True, timeout=150)
    if result.returncode:
        if result.returncode == 255:
            raise RuntimeError('SSH 连接失败，请检查网络与登录配置。\n' + result.stderr.strip())
        details = result.stderr.strip().splitlines()
        message = next((line.split('AMF_VIEWER_ERROR:', 1)[1].strip()
                        for line in details if line.startswith('AMF_VIEWER_ERROR:')),
                       details[-1] if details else '远程程序未返回错误详情')
        raise RuntimeError('服务器已连接，但查看器启动失败：' + message)
    return json.loads(result.stdout)


def remote(run=None):
    if run:
        return remote_072(run) if host_for_run(run) == 'eda072' else remote_099(run)
    return combined_catalog(remote_099)


def read_api(port, path='api/frames'):
    try:
        with urlopen(f'http://127.0.0.1:{port}/{path}', timeout=2) as response:
            return json.load(response)
    except OSError:
        return None


def connect(run):
    service = remote(run)
    on_072 = host_for_run(run) == 'eda072'
    target = 'eda072' if on_072 else SERVER
    options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10'] if on_072 else ssh_options()
    free = None
    for port in range(18767, 18800):
        with socket.socket() as sock:
            sock.settimeout(.1)
            used = sock.connect_ex(('127.0.0.1', port)) == 0
        if used:
            data = read_api(port)
            lifecycle = read_api(port, 'api/viewer-lifecycle') if data else None
            if data and data.get('directory') == service['directory'] and lifecycle and lifecycle.get('version') == 1:
                return f'http://127.0.0.1:{port}/'
        elif free is None:
            free = port
    if free is None:
        raise RuntimeError('No free local viewer port')
    subprocess.run(['ssh', *options, '-f', '-N', '-o', 'ExitOnForwardFailure=yes',
                    '-o', 'ServerAliveInterval=30', '-o', 'ServerAliveCountMax=3',
                    '-L', f"127.0.0.1:{free}:127.0.0.1:{service['port']}", target],
                   check=True, timeout=30)
    for _ in range(30):
        data = read_api(free)
        lifecycle = read_api(free, 'api/viewer-lifecycle') if data else None
        if data and data.get('directory') == service['directory'] and lifecycle and lifecycle.get('version') == 1:
            return f'http://127.0.0.1:{free}/'
        time.sleep(.5)
    raise RuntimeError('Viewer tunnel did not become ready')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--run':
        print(connect(sys.argv[2]))
        return
    data_path = ROOT / 'amf-picker-data.json'
    registered_path = ROOT / 'amf-registered-records.json'
    if registered_path.is_file():
        print('Loading saved catalog and registered previews; server refresh remains explicit...')
        saved = json.loads(data_path.read_text(encoding='utf-8-sig'))['runs'] if data_path.exists() else []
        unique = {row['run']:row for row in saved}
        unique.update({row['run']:row for row in json.loads(registered_path.read_text())['runs']})
        runs = sorted(unique.values(),key=lambda row:row.get('modified',0),reverse=True)
    else:
        print('Loading placement version list only...')
        runs = remote()
    token = secrets.token_hex(16)
    if data_path.exists():
        token = json.loads(data_path.read_text(encoding='utf-8-sig'))['token']
    data_path.write_text(json.dumps({'token': token, 'runs': runs}), encoding='utf-8')
    health = read_api(18760, 'api/health')
    if health and (health.get('app') != 'amf-lazy-picker' or health.get('root') != str(ROOT)):
        raise RuntimeError('Picker port 18760 belongs to another launcher')
    if not health:
        with (ROOT / 'picker.log').open('ab') as log:
            subprocess.Popen([sys.executable, str(ROOT / 'amf-picker-server.py')],
                             stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             start_new_session=True)
        for _ in range(30):
            health = read_api(18760, 'api/health')
            if health and health.get('app') == 'amf-lazy-picker':
                break
            time.sleep(.2)
        else:
            raise RuntimeError('Picker did not start; check picker.log')
    webbrowser.open('http://127.0.0.1:18760/')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
