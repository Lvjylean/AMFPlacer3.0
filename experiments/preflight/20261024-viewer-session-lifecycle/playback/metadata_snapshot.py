"""Reuse metadata from the same live viewer without rescanning a large netlist ZIP."""
import gzip
import hashlib
import json
from collections import Counter
from pathlib import Path
from urllib.request import urlopen


def signature(config_path):
    config_path = Path(config_path).resolve()
    config = json.loads(config_path.read_text())
    paths = [config_path, Path(config['vivado extracted design information file']),
             Path(config['vivado extracted device information file']),
             Path(config['physical boundary model file']).parent / 'boundaries.json']
    virtual = config_path.parent / 'visualization-virtual-cells.json'
    if virtual.exists():
        paths.append(virtual)
    return dict(config_sha256=hashlib.sha256(config_path.read_bytes()).hexdigest(),
                files=[[str(p.resolve()), p.stat().st_size, p.stat().st_mtime_ns] for p in paths])


def capture(base_url, config_path, destination):
    """Explicit migration helper; only reads the existing loopback viewer APIs."""
    before = signature(config_path)
    metadata = json.load(urlopen(base_url + '/api/metadata', timeout=30))
    if Path(metadata['source']).resolve() != Path(config_path).resolve():
        raise ValueError('Viewer metadata belongs to another run')
    with urlopen(base_url + '/api/live-dictionary', timeout=60) as response:
        raw = response.read()
        if response.headers.get('Content-Encoding') == 'gzip':
            raw = gzip.decompress(raw)
    dictionary = json.loads(raw)
    types = {n: t for n, t in zip(dictionary['names'], dictionary['types']) if t != 'UNKNOWN'}
    if dict(Counter(types.values())) != metadata['counts']:
        raise ValueError('Live dictionary does not match the original type counts')
    if before != signature(config_path):
        raise ValueError('Metadata inputs changed during capture')
    result = dict(signature=before, metadata=metadata, types=types, source_viewer=base_url)
    body = gzip.compress(json.dumps(result, separators=(',', ':')).encode(), compresslevel=1)
    if len(body) > 12 * 1024**2:
        raise ValueError('Metadata snapshot exceeds 12 MiB budget')
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise ValueError('Refusing to overwrite a metadata snapshot')
    destination.write_bytes(body)
    return dict(bytes=len(body), cells=len(types), sha256=hashlib.sha256(body).hexdigest())


def load(path, config_path):
    with gzip.open(path, 'rt') as source:
        data = json.load(source)
    if data['signature'] != signature(config_path):
        raise ValueError('Metadata snapshot is stale; original inputs changed')
    return data['types'], data['metadata']
