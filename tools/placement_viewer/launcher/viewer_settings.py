"""Machine-specific viewer settings; never part of the placer configuration."""
import json
import os
from pathlib import Path


def load_settings():
    path = Path(os.environ.get('AMF_VIEWER_SETTINGS',
                               Path(__file__).with_name('viewer_settings.json'))).expanduser()
    if not path.is_file():
        raise ValueError('Copy viewer_settings.example.json to viewer_settings.json and set SSH host and paths')
    data = json.loads(path.read_text())
    for key in ('ssh_host', 'remote_project_root', 'remote_tools_dir'):
        if not isinstance(data.get(key), str) or not data[key].strip():
            raise ValueError('Missing viewer setting: ' + key)
    if data['ssh_host'].startswith('-') or any(c.isspace() for c in data['ssh_host']):
        raise ValueError('ssh_host must be an SSH alias or user@host')
    for key in ('remote_project_root', 'remote_tools_dir'):
        if not data[key].startswith('/'):
            raise ValueError(key + ' must be an absolute server path')
    port = data.get('picker_port', 18760)
    if type(port) is not int or not 1024 <= port <= 65535:
        raise ValueError('picker_port must be between 1024 and 65535')
    data['picker_port'] = port
    return data
