"""Compare complete physical route trees; ignore only sibling branch order."""
from collections import Counter
from pathlib import Path
import hashlib
import json
import re
import sys
import time


def graph(text):
    nodes, edges, roots = Counter(), Counter(), Counter()
    stack, previous = [], None
    for token in re.findall(r'\{|\}|[^\s{}]+', text):
        if token == '{':
            stack.append(previous)
        elif token == '}':
            if not stack:
                raise ValueError('Unbalanced route braces')
            previous = stack.pop()
        else:
            if '\\' in token or '"' in token:
                raise ValueError('Unsupported escaped route token')
            nodes[token] += 1
            if previous is None:
                roots[token] += 1
            else:
                edges[previous, token] += 1
            previous = token
    if stack or not nodes:
        raise ValueError('Unbalanced or empty route')
    return nodes, edges, roots


def main():
    start = time.monotonic()
    before, after, dest = map(Path, sys.argv[1:])
    raw_a, raw_b = before.read_bytes(), after.read_bytes()
    a, b = graph(raw_a.decode('utf-8')), graph(raw_b.decode('utf-8'))
    result = dict(
        method='Complete token nodes, directed parent-child edges and roots with multiplicities; only branch order ignored. NoTile annotation tokens retained.',
        before=str(before), after=str(after),
        before_sha256=hashlib.sha256(raw_a).hexdigest(),
        after_sha256=hashlib.sha256(raw_b).hexdigest(),
        raw_text_equal=raw_a == raw_b,
        tokens_equal=a[0] == b[0], directed_edges_equal=a[1] == b[1], roots_equal=a[2] == b[2],
        node_occurrences_before=sum(a[0].values()), node_occurrences_after=sum(b[0].values()),
        edge_occurrences_before=sum(a[1].values()), edge_occurrences_after=sum(b[1].values()),
        removed_edge_count=sum((a[1]-b[1]).values()), added_edge_count=sum((b[1]-a[1]).values()),
        removed_edge_sample=list((a[1]-b[1]).items())[:5], added_edge_sample=list((b[1]-a[1]).items())[:5],
        seconds=time.monotonic()-start)
    result['passed'] = a == b
    dest.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    if not result['passed']:
        raise SystemExit('Fixed clock physical route changed')


if __name__ == '__main__':
    main()
