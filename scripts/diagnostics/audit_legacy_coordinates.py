"""Audit saved VCU108 fabric coordinates against its legacy exporter rules.

Not a U250 converter. Unsupported site families are reported, not approximated.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import zipfile


def expected(site, tile, site_type):
    tm = re.search(r'_X(\d+)Y(\d+)$', tile)
    sm = re.search(r'_X(\d+)Y(\d+)$', site)
    if not tm or not sm:
        raise ValueError('Malformed site/tile name')
    x, y = map(int, tm.groups())
    sy = int(sm[2])
    if not (site.startswith('SLICE_') or site.startswith('DSP48E2_') or site.startswith('RAMB18_') or site.startswith('RAMB36_')):
        return None
    # Deliberately reproduce sequential conditions, not two tests on original X.
    if x >= 34:
        x += 1
    if x >= 53:
        x += 1
    left = not ('_R_' in tile or 'DSP' in tile or tile.startswith('PCIE_X'))
    if 'CLE_M_R_X84' in tile:
        left = True
    x += -0.25 if left else 0.25
    if site.startswith('SLICE_'):
        offset = 0.0
    elif site.startswith('DSP48E2_'):
        offset = 3.425 if sy % 2 else 0.925
    elif site_type == 'RAMBFIFO18':
        offset = 0.925 + (0.35 if sy % 2 else 0)
    elif site_type == 'RAMB181':
        offset = 3.075 + (0.35 if sy % 2 else 0)
    elif site_type == 'RAMBFIFO36':
        # Literal exporter behavior also applies its site-Y parity shift to RAMB36.
        offset = 2.0 + (0.35 if sy % 2 else 0)
    else:
        raise ValueError('Unsupported fabric type: ' + site_type)
    return x, y + offset


def audit(path):
    counts, skipped, differences = Counter(), Counter(), []
    maxima = {}
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if not n.endswith('/')]
        if len(names) != 1:
            raise ValueError('Expected one device text member')
        data = z.read(names[0])
    for line in data.decode().splitlines():
        fields = dict(re.findall(r'(\w+)=>\s*(\S+)', line))
        if not fields:
            continue
        site, tile, typ = (fields[k] for k in ('site', 'tile', 'sitetype'))
        target = expected(site, tile, typ)
        if target is None:
            skipped[typ] += 1
            continue
        counts[typ] += 1
        actual = float(fields['centerx']), float(fields['centery'])
        error = max(abs(a-b) for a,b in zip(actual, target))
        maxima[typ] = max(maxima.get(typ, 0), error)
        if error > 1e-8:
            differences.append(dict(site=site, actual=actual, expected=target))
    return dict(schema='amf-legacy-coordinate-audit-v1', input=str(path.resolve()),
                archive_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                payload_sha256=hashlib.sha256(data).hexdigest(),
                checked_by_type=dict(counts), skipped_by_type=dict(skipped),
                max_error_by_type=maxima, mismatch_count=len(differences),
                mismatch_examples=differences[:30],
                scope='VCU108 archive at recorded input path; main fabric types only; not U250')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.archive)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(bool(result['mismatch_count']))
