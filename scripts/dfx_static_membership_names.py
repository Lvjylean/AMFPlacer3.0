"""Classify immutable bulk-exported names; never accesses or changes a design."""
import hashlib
import json
from pathlib import Path
import sys
import time


def classify(names, direct, hierarchy, rps):
    missing, unexpected, cache = [], [], {}
    static_count = 0
    for name in names:
        dynamic = any(name == rp or name.startswith(rp + '/') for rp in rps)
        owned = name in direct
        if not owned:
            parent = name.rsplit('/', 1)[0] if '/' in name else ''
            trail = []
            while parent:
                if parent in cache:
                    owned = cache[parent]
                    break
                trail.append(parent)
                if parent in hierarchy:
                    owned = True
                    break
                parent = parent.rsplit('/', 1)[0] if '/' in parent else ''
            cache.update((p, owned) for p in trail)
        if dynamic:
            if owned:
                unexpected.append(name)
        else:
            static_count += 1
            if not owned:
                missing.append(name)
    return static_count, missing, unexpected


def run(prefix):
    start = time.monotonic()
    paths = {tag: Path(str(prefix) + '_' + tag + '.txt')
             for tag in ('primitives', 'direct', 'hierarchy', 'rps')}
    data, hashes = {}, {}
    for tag, path in paths.items():
        blob = path.read_bytes()
        hashes[tag] = hashlib.sha256(blob).hexdigest()
        decoded = blob.decode()
        values = decoded.splitlines() if decoded.strip('\n') else []
        if len(values) != len(set(values)) or any(not n for n in values):
            raise ValueError('Empty or duplicate name in ' + tag)
        data[tag] = values
    if not set(data['hierarchy']).issubset(data['direct']):
        raise ValueError('Hierarchy member outside direct membership')
    static_count, missing, unexpected = classify(data['primitives'], set(data['direct']),
                                                set(data['hierarchy']), data['rps'])
    for tag, values in [('missing', missing), ('unexpected', unexpected)]:
        Path(str(prefix) + '_' + tag + '.txt').write_text(''.join(n + '\n' for n in values))
    Path(str(prefix) + '_static_count.txt').write_text(str(static_count) + '\n')
    result = dict(input_counts={tag: len(v) for tag, v in data.items()}, input_hashes=hashes,
                  static_count=static_count, missing=len(missing), unexpected=len(unexpected),
                  seconds=time.monotonic() - start)
    Path(str(prefix) + '_classification.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    print(json.dumps(run(Path(sys.argv[1]))))
