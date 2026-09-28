#!/usr/bin/env python3
"""Classify full EDIF diffs; ignore only explicitly named nonfunctional metadata.

SOFT_HLUTNM is a packing hint. NETLIST_CHECKSUM/ECO_CHECKSUM are bookkeeping.
Unknown properties, topology, interfaces, names and primitive functions fail closed.
The full unfiltered differences are always preserved by compare_functional_edif.py.
"""
import collections
import json
from pathlib import Path
import sys


def filter_metadata(node, allowed, removed):
    if not isinstance(node, list):
        return node
    if node and node[0] == 'property':
        name = node[1]
        if isinstance(name, list) and name[0] == 'rename':
            name = name[2].strip('"')
        if name in allowed:
            removed[name] += 1
            return None
    result = []
    for child in node:
        value = filter_metadata(child, allowed, removed)
        if value is not None:
            result.append(value)
    return result


def main(path):
    counts = collections.Counter()
    removed_before, removed_after = collections.Counter(), collections.Counter()
    examples = []
    with path.open() as stream:
        for line in stream:
            item = json.loads(line)
            if item['kind'] == 'status':
                counts['status_metadata'] += 1
                continue
            allowed = {'SOFT_HLUTNM'} if item['kind'] == 'instance' else set()
            if item['kind'] == 'root' or (item['kind'] == 'cell' and item['scope'] == 'work' and item['name'] == 'kernel_getrf_0'):
                allowed |= {'NETLIST_CHECKSUM', 'ECO_CHECKSUM'}
            a = filter_metadata(item['before'], allowed, removed_before)
            b = filter_metadata(item['after'], allowed, removed_after)
            if item['change'] == 'changed' and a == b:
                counts['nonfunctional_metadata_only'] += 1
            else:
                counts['unexplained_or_functional_difference'] += 1
                if len(examples) < 10:
                    examples.append({k:v for k,v in item.items() if k not in {'before','after'}})
    result = dict(diff_file=str(path), classifications=dict(counts),
                  metadata_before=dict(removed_before), metadata_after=dict(removed_after),
                  structural_equivalence_pass=counts['unexplained_or_functional_difference']==0,
                  unexplained_examples=examples,
                  proof_scope='plaintext EDIF structural identity; encrypted sidecar internals require separate native-object audit; not SAT or RTL equivalence')
    path.with_suffix('.classification.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main(Path(sys.argv[1]))
