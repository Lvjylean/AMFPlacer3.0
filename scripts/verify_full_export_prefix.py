#!/usr/bin/env python3
"""Compare every complete cell in a stopped scalar export before running AMF.

The partial last cell is excluded. This is partial large-design equivalence,
not a claim that an unfinished scalar export proves the whole design identical.
"""
import hashlib
import json
from pathlib import Path


def verify(reference, candidate):
    reference, candidate = Path(reference), Path(candidate)
    last_header = None
    cells = 0
    with (reference / 'netlist.txt').open('rb') as f:
        while True:
            offset = f.tell()
            line = f.readline()
            if not line:
                break
            if line.startswith(b'curCell=> '):
                last_header = offset
                cells += 1
    if last_header is None or cells < 2:
        raise ValueError('Reference contains no complete cell prefix')
    digest = hashlib.sha256()
    lines = 0
    with (reference / 'netlist.txt').open('rb') as old, (candidate / 'netlist.txt').open('rb') as new:
        while old.tell() < last_header:
            a, b = old.readline(), new.readline()
            lines += 1
            if a != b:
                raise ValueError('Scalar/batch export differs at line %d: %r vs %r' % (lines, a, b))
            digest.update(a)
    masks = {}
    for name in ('hard_regions.tsv', 'hard_region_sites.tsv', 'infrastructure.tsv'):
        a, b = (reference / name).read_bytes(), (candidate / name).read_bytes()
        if a != b:
            raise ValueError('Export differs: ' + name)
        masks[name] = hashlib.sha256(a).hexdigest()
    result = dict(scope='complete prefix of interrupted scalar export',
                  whole_design_equivalence_proven=False, reference=str(reference),
                  complete_cells=cells - 1, lines=lines, bytes=last_header,
                  identical=True, prefix_sha256=digest.hexdigest(), masks=masks)
    (candidate / 'export_prefix_equivalence.json').write_text(json.dumps(result, indent=2) + '\n')
    return result
