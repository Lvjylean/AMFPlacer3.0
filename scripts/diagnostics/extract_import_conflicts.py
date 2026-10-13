"""Extract site-level reproductions without changing a completed experiment."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re


def category(message):
    for key in ('OUTMUXC', 'FFMUXA1', 'SET/RESET', '10 input pins', 'shape'):
        if key in message:
            return key
    return 'other'


def extract(run, out):
    run, out = Path(run).resolve(), Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    assignments = dict(line.split('\t') for line in (run/'placement/requested.tsv').read_text().splitlines())
    sites = collections.defaultdict(list)
    for cell, target in assignments.items():
        sites[target.split('/')[0]].append((cell, target))
    lines = (run/'logs/vivado.log').read_text().splitlines()
    events = []
    for i, line in enumerate(lines):
        if not re.match(r'AMF_IMPORT(?:_RETRY)?_REJECTED:', line):
            continue
        match = re.search(r'instance\(s\) (.*?)\.\s*$', line)
        if not match:
            raise ValueError('Unparsed rejection: '+line)
        cell = match[1]
        shape = re.search(r'shape which contains instance (.*?) which has', line)
        if shape:
            cell = shape[1]
        message = line+'\n'+lines[i+1]
        events.append(dict(cell=cell, site=assignments[cell].split('/')[0],
                           category=category(message), message=message,
                           phase='retry' if '_RETRY_' in line else 'initial'))
    selected = {}
    categories = collections.defaultdict(set)
    for event in events:
        categories[event['category']].add(event['site'])
    for kind, candidates in sorted(categories.items()):
        for site in sorted(candidates)[:3]:
            selected.setdefault(site, []).append(kind)
    bad_sites = {e['site'] for e in events}
    for site in sorted(sites):
        if site.startswith('SLICE_') and site not in bad_sites and len(sites[site]) >= 20:
            selected[site] = ['control']
            if sum(v == ['control'] for v in selected.values()) == 3:
                break
    with (out/'sites.tsv').open('w') as f:
        for site, kinds in selected.items():
            for cell, target in sites[site]:
                f.write('\t'.join((site, ','.join(kinds), cell, target))+'\n')
    payload = dict(source_run=str(run), source_commit=json.loads((run/'manifest.json').read_text())['source_commit'],
                   events=events, error_sites=len(bad_sites),
                   categories={k:len(v) for k,v in categories.items()}, selected=selected,
                   source_sha256={str(p.relative_to(run)):hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (run/'placement/requested.tsv', run/'logs/vivado.log', run/'manifest.json')})
    (out/'conflicts.json').write_text(json.dumps(payload,indent=2)+'\n')
    print(json.dumps({k:payload[k] for k in ('source_run','error_sites','categories','selected')},indent=2))


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('run');p.add_argument('out')
    a=p.parse_args();extract(a.run,a.out)
