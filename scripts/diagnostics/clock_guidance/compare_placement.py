"""Exact complete NAME/LOC/BEL comparison, without Tcl per-key lookups."""
from pathlib import Path
import hashlib
import json
import sys
import time


def read(prefix):
    columns = []
    hashes = {}
    count = int(Path(str(prefix) + '.count').read_text())
    if count <= 0:
        raise ValueError('Empty placement inventory')
    for suffix in ('.names', '.locs', '.bels'):
        data = Path(str(prefix) + suffix).read_bytes()
        rows = data.decode('utf-8').splitlines()
        if len(rows) != count:
            raise ValueError(f'Incomplete {suffix} vector: {len(rows)} != {count}')
        columns.append(rows)
        hashes[suffix] = hashlib.sha256(data).hexdigest()
    names, locs, bels = columns
    result = dict(zip(names, zip(locs, bels)))
    if len(result) != count or '' in result:
        raise ValueError('Duplicate or empty cell names')
    return result, hashes


def main():
    start = time.monotonic()
    before, before_hash = read(sys.argv[1])
    after, after_hash = read(sys.argv[2])
    dest = Path(sys.argv[3])
    missing = before.keys() - after.keys()
    extra = after.keys() - before.keys()
    changed = [name for name in before.keys() & after.keys()
               if before[name] != after[name]]
    summary = dict(before=len(before), after=len(after), missing=len(missing),
                   changed=len(changed), extra=len(extra),
                   before_vector_sha256=before_hash, after_vector_sha256=after_hash,
                   seconds=time.monotonic() - start)
    Path(str(dest) + '_placement_summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    Path(str(dest) + '_placement_summary.tsv').write_text(
        'before\tafter\tmissing\tchanged\textra\n' +
        '\t'.join(str(summary[k]) for k in ('before', 'after', 'missing', 'changed', 'extra')) + '\n')
    with Path(str(dest) + '_placement_changes.jsonl').open('w') as f:
        for name in sorted(missing | extra | set(changed)):
            f.write(json.dumps(dict(name=name, before=before.get(name), after=after.get(name))) + '\n')
    print(json.dumps(summary))
    if missing or changed or extra:
        raise SystemExit('Full design placement changed')


if __name__ == '__main__':
    main()
