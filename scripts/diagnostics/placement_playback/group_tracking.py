"""Cell groups keyed by immutable dictionary IDs; all runtime caches bounded in RAM."""
import array
import collections
import gzip
import hashlib
import json
import sys
import threading
from pathlib import Path


class GroupTracker:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.cache = self.directory / '.viewer-binary-v1'
        self.lock = threading.RLock()
        self.dictionary = None
        self.groups = collections.OrderedDict()
        self.overlays = collections.OrderedDict()
        self.overlay_bytes = 0

    def initialize(self):
        with self.lock:
            if self.dictionary is not None:
                return
            if not (self.cache / 'manifest.json').exists():
                raise ValueError('请等待本轮二进制缓存完成后使用分组追踪')
            self.dictionary = json.loads(gzip.decompress((self.cache / 'dictionary.json.gz').read_bytes()))
            self.lookup = {name: i for i, name in enumerate(self.dictionary['names'])}
            self.valid_frames = {v[0] for v in json.loads((self.cache / 'manifest.json').read_text())['signature']['frames']}
            modules = collections.Counter()
            for name, kind in zip(self.dictionary['names'], self.dictionary['types']):
                if kind == 'VIRTUAL':
                    continue
                parts = name.split('/')
                for depth in range(1, min(3, len(parts)-1)+1):
                    modules['/'.join(parts[:depth])] += 1
            self.modules = sorted(modules.items(), key=lambda x: (-x[1], x[0]))

    def suggestions(self, query=''):
        self.initialize()
        matches = [(p, n) for p, n in self.modules if query.lower() in p.lower()]
        return {'modules': [{'prefix': p, 'count': n} for p, n in matches[:100]],
                'total': len(matches), 'note': '建议显示前三层；可手工输入任意完整层级路径'}

    def select(self, spec):
        self.initialize()
        if not isinstance(spec, dict):
            raise ValueError('分组必须是 JSON 对象')
        prefix = spec.get('prefix')
        cells = spec.get('cells')
        if (prefix is None) == (cells is None):
            raise ValueError('每组应指定 prefix 或 cells，不能同时指定')
        excluded = []
        missing = []
        duplicates = 0
        if prefix is not None:
            if not isinstance(prefix, str) or not prefix.strip().strip('/'):
                raise ValueError('模块路径不能为空')
            prefix = prefix.strip().rstrip('/')
            ids = [i for i, n in enumerate(self.dictionary['names']) if n.startswith(prefix + '/')]
            requested = len(ids)
        else:
            if not isinstance(cells, list) or len(cells) > 1000000 or any(not isinstance(n, str) for n in cells):
                raise ValueError('cells 必须是单元名称字符串数组，最多一百万项')
            clean = [n.strip() for n in cells if n.strip()]
            unique = list(dict.fromkeys(clean))
            duplicates = len(clean)-len(unique)
            requested = len(unique)
            missing = [n for n in unique if n not in self.lookup]
            ids = [self.lookup[n] for n in unique if n in self.lookup]
        if spec.get('include_virtual') is not True:
            excluded = [i for i in ids if self.dictionary['types'][i] == 'VIRTUAL']
            ids = [i for i in ids if self.dictionary['types'][i] != 'VIRTUAL']
        ids = sorted(ids)
        if not ids:
            raise ValueError(f'没有匹配到可追踪单元；未匹配 {len(missing)}，排除虚拟占位 {len(excluded)}')
        mask = bytearray(len(self.dictionary['names']))
        for i in ids:
            mask[i] = 1
        token = hashlib.sha256(mask).hexdigest()[:24]
        with self.lock:
            self.groups[token] = bytes(mask)
            self.groups.move_to_end(token)
            while len(self.groups) > 4:
                old, _ = self.groups.popitem(last=False)
                for key in list(self.overlays):
                    if key[0] == old:
                        self.overlay_bytes -= len(self.overlays.pop(key))
        return {'token': token, 'name': str(spec.get('name') or prefix or 'Cell group')[:200],
                'ids': ids, 'matched': len(ids), 'requested': requested, 'duplicates': duplicates,
                'missing_count': len(missing), 'missing': missing, 'excluded_virtual': len(excluded),
                'resource_counts': dict(collections.Counter(self.dictionary['types'][i] for i in ids)),
                'identity': 'post-synthesis exact cell names; fixed dictionary IDs across frames'}

    def frame(self, token, name):
        self.initialize()
        if name not in self.valid_frames:
            raise ValueError('无效的轨迹帧')
        key = (token, name)
        with self.lock:
            if token not in self.groups:
                raise ValueError('该分组已从内存淘汰或服务已重启，请重新应用分组')
            self.groups.move_to_end(token)
            if key in self.overlays:
                self.overlays.move_to_end(key)
                return self.overlays[key]
            mask = self.groups[token]
        data = gzip.decompress((self.cache / (name + '.bin.gz')).read_bytes())
        if len(data) % 12:
            raise ValueError('无效的轨迹数据')
        values = array.array('I')
        values.frombytes(data)
        if sys.byteorder != 'little':
            values.byteswap()
        if any(i >= len(mask) for i in values[::3]):
            raise ValueError('轨迹 ID 超出字典范围')
        selected = b''.join(data[j*12:j*12+12] for j, i in enumerate(values[::3]) if mask[i])
        body = gzip.compress(selected, compresslevel=1)
        with self.lock:
            # Another request can have completed the same frame while we were reading.
            if key not in self.overlays:
                self.overlays[key] = body
                self.overlay_bytes += len(body)
            while self.overlay_bytes > 32*1024*1024 or len(self.overlays) > 24:
                _, old = self.overlays.popitem(last=False)
                self.overlay_bytes -= len(old)
        return body
