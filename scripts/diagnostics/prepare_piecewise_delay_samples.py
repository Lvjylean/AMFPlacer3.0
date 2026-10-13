#!/usr/bin/env python3
"""Deterministic stratified ordinary-fabric net sample, independent of timing rank."""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import heapq
import json
from pathlib import Path
import zipfile

from analyze_boundary_timing_samples import load_model, crossings


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def netlist_lines(path):
    with zipfile.ZipFile(path) as z, z.open(z.namelist()[0]) as f:
        for raw in f: yield raw.decode().split()


def family(kind):
    if kind.startswith('LUT'): return 'LUT'
    if kind in ('FDRE', 'FDSE', 'FDCE', 'FDPE'): return 'FF'
    return None


def prepare(args):
    sites, cuts = load_model(args.model)
    with args.locations.open() as f:
        locs = {r['cell']: r['site'] for r in csv.DictReader(f, delimiter='\t') if r['site'] in sites}
    types = {}; fanout = Counter()
    for p in netlist_lines(args.netlist):
        if p and p[0] == 'curCell=>': types[p[1]] = p[3]
        elif p and p[0] == 'pin=>' and p[p.index('dir=>')+1] == 'IN':
            d = p[p.index('drivepin=>')+1:]
            if d: fanout[d[0]] += 1
    # One randomly ranked eligible sink per driver prevents high-fanout nets
    # from contributing many correlated observations to train or holdout.
    chosen = {}; exclusions = Counter()
    for p in netlist_lines(args.netlist):
        if not p or p[0] != 'pin=>' or p[p.index('dir=>')+1] != 'IN': continue
        drivers = p[p.index('drivepin=>')+1:]
        if not drivers: continue
        driver, sink = drivers[0], p[1]
        a, b = driver.rsplit('/', 1)[0], sink.rsplit('/', 1)[0]
        af, bf = family(types.get(a, '')), family(types.get(b, ''))
        if not af or not bf or (bf == 'FF' and p[p.index('refpin=>')+1] != 'D'): continue
        if not 1 <= fanout[driver] <= 8: exclusions['fanout'] += 1; continue
        if a not in locs or b not in locs: exclusions['location'] += 1; continue
        ap, bp = sites[locs[a]], sites[locs[b]]
        if ap[2] != bp[2] or crossings(ap, bp, cuts): exclusions['boundary'] += 1; continue
        dx, dy = abs(ap[0]-bp[0]), abs(ap[1]-bp[1])
        length = dx + .66*dy  # Sampling bins only, never a fitted coefficient.
        if length < args.minimum_span: exclusions['under_minimum_span'] += 1; continue
        rank = int.from_bytes(hashlib.sha256(('piecewise-delay-v1:'+sink).encode()).digest()[:8], 'big')
        if driver not in chosen or rank < chosen[driver][0]:
            chosen[driver] = (rank, (driver, sink, dx, dy, ap[2], af, bf, fanout[driver]))
    pools = defaultdict(list); population = Counter()
    for rank, row in chosen.values():
        driver, sink, dx, dy, slr, af, bf, fo = row
        # Independent driver priority: reusing the minimum sink hash here would
        # overselect drivers with more eligible loads even within an FO stratum.
        rank = int.from_bytes(hashlib.sha256(('piecewise-delay-v1:driver:'+driver).encode()).digest()[:8], 'big')
        xn, yn = dx/39.5, dy/59.9375
        orientation = 'x' if xn > 2*yn else 'y' if yn > 2*xn else 'mixed'
        length = dx+.66*dy
        radius2 = dx*dx+dy*dy
        band = 'near' if radius2 < 9 else 'middle' if radius2 < 36 else 'far-6-30' if radius2 < 900 else 'far-30+'
        stratum = f'{slr}:{af}-{bf}:{1 if fo == 1 else 2}:{orientation}:{band}'
        population[stratum] += 1
        item = (-rank, row)
        if len(pools[stratum]) < args.per_stratum: heapq.heappush(pools[stratum], item)
        elif item > pools[stratum][0]: heapq.heapreplace(pools[stratum], item)
    args.output.mkdir(parents=True, exist_ok=False)
    with (args.output/'requests.tsv').open('w') as f:
        w = csv.writer(f, delimiter='\t'); w.writerow(['source_pin','sink_pin','dx','dy','slr','source_family','sink_family','original_fanout','stratum','population','sample_count'])
        for stratum, entries in sorted(pools.items()):
            for _, row in sorted(entries): w.writerow([*row, stratum, population[stratum], len(entries)])
    report = dict(schema='piecewise-delay-sampling-v1', seed='piecewise-delay-v1', requests=sum(map(len,pools.values())),
                  eligible_drivers=len(chosen), exclusions=dict(exclusions), per_stratum=args.per_stratum, minimum_span=args.minimum_span,
                  selection='one eligible sink per driver; hash rank; balanced SLR/type/fanout/orientation/distance strata',
                  strata=dict(population), inputs={str(p):digest(p) for p in (args.model,args.locations,args.netlist)})
    (args.output/'sampling.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('strata','inputs')}),flush=True)


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('model','locations','netlist','output'): p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--per-stratum',type=int,default=60)
    p.add_argument('--minimum-span',type=float,default=0)
    prepare(p.parse_args())
