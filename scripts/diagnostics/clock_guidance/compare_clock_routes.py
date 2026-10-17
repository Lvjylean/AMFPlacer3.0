"""Compare saved FIXED_ROUTE trees; no Vivado/design mutations or queries."""
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
                raise ValueError('Unsupported escaped token')
            nodes[token] += 1
            if previous is None:
                roots[token] += 1
            else:
                edges[previous, token] += 1
            previous = token
    if stack:
        raise ValueError('Unbalanced route braces')
    return nodes, edges, roots


def main():
    start = time.monotonic()
    before, after, dest = map(Path, sys.argv[1:])
    raw_a, raw_b = before.read_bytes(), after.read_bytes()
    a, b = graph(raw_a.decode()), graph(raw_b.decode())
    result = {
        'method': 'Complete FIXED_ROUTE token nodes, directed parent-child edges and roots; ignore branch order. NoTile delay-arc annotation tokens retained.',
        'before': str(before), 'after': str(after),
        'before_sha256': hashlib.sha256(raw_a).hexdigest(),
        'after_sha256': hashlib.sha256(raw_b).hexdigest(),
        'raw_text_equal': raw_a == raw_b,
        'tokens_equal': a[0] == b[0],
        'directed_edges_equal': a[1] == b[1],
        'roots_equal': a[2] == b[2],
        'node_occurrences_before': sum(a[0].values()),
        'node_occurrences_after': sum(b[0].values()),
        'edge_occurrences_before': sum(a[1].values()),
        'edge_occurrences_after': sum(b[1].values()),
        'removed_edge_count': sum((a[1] - b[1]).values()),
        'added_edge_count': sum((b[1] - a[1]).values()),
        'seconds': time.monotonic() - start,
        'scope': 'Saved fixed-clock route only; does not replace full pr_verify.'
    }
    dest.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
