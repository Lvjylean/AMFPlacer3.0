#!/usr/bin/env python3
"""macOS launcher with on-demand SSH connections and personal credentials."""
import shlex
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
from viewer_settings import load_settings

ROOT = Path(__file__).resolve().parent


def ssh_options():
    options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', '-o', 'IdentitiesOnly=yes']
    key_name = os.environ.get('AMF_SSH_KEY') or load_settings().get('ssh_key', '')
    key = Path(key_name).expanduser() if key_name else None
    if key is not None and key.is_file():
        options += ['-i', str(key)]
    elif key_name:
        raise RuntimeError(f'SSH key does not exist: {key}')
    return options


def remote(run=None):
    settings = load_settings()
    command = ["env", "AMF_VIEWER_PROJECT_ROOT=" + settings['remote_project_root'],
               "AMF_VIEWER_HOST_LABEL=" + settings['ssh_host'],
               "python3", settings['remote_tools_dir'].rstrip('/') + '/launcher/amf-viewer-remote.py']
    if run:
        command.append(run)
    result = subprocess.run(['ssh', *ssh_options(), settings['ssh_host'], shlex.join(command)],
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


def read_api(port, path='api/frames'):
    try:
        with urlopen(f'http://127.0.0.1:{port}/{path}', timeout=2) as response:
            return json.load(response)
    except OSError:
        return None


def connect(run):
    settings = load_settings()
    service = remote(run)
    free = None
    for port in range(18767, 18800):
        with socket.socket() as sock:
            sock.settimeout(.1)
            used = sock.connect_ex(('127.0.0.1', port)) == 0
        if used:
            data = read_api(port)
            if data and data.get('directory') == service['directory']:
                return f'http://127.0.0.1:{port}/'
        elif free is None:
            free = port
    if free is None:
        raise RuntimeError('No free local viewer port')
    subprocess.run(['ssh', *ssh_options(), '-f', '-N', '-o', 'ExitOnForwardFailure=yes',
                    '-o', 'ServerAliveInterval=30', '-o', 'ServerAliveCountMax=3',
                    '-L', f"127.0.0.1:{free}:127.0.0.1:{service['port']}", settings['ssh_host']],
                   check=True, timeout=30)
    for _ in range(30):
        data = read_api(free)
        if data and data.get('directory') == service['directory']:
            return f'http://127.0.0.1:{free}/'
        time.sleep(.5)
    raise RuntimeError('Viewer tunnel did not become ready')


def main():
    port = load_settings()['picker_port']
    if len(sys.argv) == 3 and sys.argv[1] == '--run':
        print(connect(sys.argv[2]))
        return
    print('Loading placement version list only...')
    runs = remote()
    data_path = ROOT / 'amf-picker-data.json'
    token = secrets.token_hex(16)
    if data_path.exists():
        token = json.loads(data_path.read_text(encoding='utf-8-sig'))['token']
    data_path.write_text(json.dumps({'token': token, 'runs': runs}), encoding='utf-8')
    health = read_api(port, 'api/health')
    if health and (health.get('app') != 'amf-lazy-picker' or health.get('root') != str(ROOT)):
        raise RuntimeError(f'Picker port {port} belongs to another launcher')
    if not health:
        with (ROOT / 'picker.log').open('ab') as log:
            subprocess.Popen([sys.executable, str(ROOT / 'amf-picker-server.py')],
                             stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             start_new_session=True)
        for _ in range(30):
            health = read_api(port, 'api/health')
            if health and health.get('app') == 'amf-lazy-picker':
                break
            time.sleep(.2)
        else:
            raise RuntimeError('Picker did not start; check picker.log')
    webbrowser.open(f'http://127.0.0.1:{port}/')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
