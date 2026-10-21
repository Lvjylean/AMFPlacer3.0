#!/usr/bin/env python3
"""Refresh complete physical pin classifications from a new static occupancy.

Only immutable device/Pblock topology is reusable. Owners are always supplied
by the current locked, empty-RP shell. The hard rule remains FF Q first-hop
blockage; a free first hop is not a general routing certificate.
"""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re
import time

HARD = 'ALL_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA'
NODE_HARD = {'SOURCE_NODE_FIXED_STATIC', 'ALL_ORDINARY_FIRST_HOPS_FIXED_STATIC'}
SP_HARD = 'ALL_MAPPED_EXTERNAL_NODES_BLOCKED_FOR_GENERAL_DATA'

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(8 << 20), b''): h.update(b)
    return h.hexdigest()

def rows(path):
    opener = gzip.open if str(path).endswith('.gz') else open
    with opener(path, 'rt') as f:
        header = next(f).rstrip('\n').split('\t')
        for line in f:
            values = line.rstrip('\n').split('\t')
            if len(values) != len(header): raise ValueError('Malformed topology row: ' + str(path))
            yield values

def writer(path, columns):
    f = gzip.open(path, 'wt', compresslevel=1)
    f.write('\t'.join(columns) + '\n')
    return f

def mapped_state(states, kind):
    if kind == 'site':
        hard, all_hard, some_hard = NODE_HARD, SP_HARD, 'SOME_MAPPED_EXTERNAL_NODES_BLOCKED_FOR_GENERAL_DATA'
        empty, special, special_result = 'NO_EXTERNAL_NODE', {'DEDICATED_OR_NO_ADJACENCY', 'CONSTANT_ONLY_OR_SPECIAL'}, 'SPECIAL_OR_DEDICATED_ACCESS'
    else:
        hard, all_hard, some_hard = {SP_HARD}, HARD, 'SOME_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA'
        empty, special, special_result = 'NO_DIRECT_SITE_PIN_MAPPING', {'SPECIAL_OR_DEDICATED_ACCESS', 'NO_EXTERNAL_NODE'}, 'SPECIAL_OR_INTERNAL_ACCESS_REQUIRES_MODEL'
    if not states: return empty
    if all(s in hard for s in states): return all_hard
    if any(s in hard for s in states): return some_hard
    if 'BOUNDARY_NET_DEPENDENT' in states: return 'BOUNDARY_NET_DEPENDENT'
    if any(s in special for s in states): return special_result
    if any('PARTIAL' in s or 'SOME_' in s for s in states): return 'PARTIAL_STATIC_FIRST_HOP_BLOCKAGE'
    if 'NONFIXED_ROUTE_POLICY_DEPENDENT' in states: return 'NONFIXED_ROUTE_POLICY_DEPENDENT'
    return 'NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP'

def classify(topology, occupancy, output):
    start = time.monotonic()
    topology, occupancy, output = map(Path, (topology, occupancy, output))
    output.mkdir(parents=True, exist_ok=False)
    stages = {}
    def tick(name, t): stages[name] = time.monotonic() - t
    t = time.monotonic()
    # Header names are read separately; column positions are never guessed.
    def indexed(name):
        p = topology / name
        with gzip.open(p, 'rt') as f: header = next(f).rstrip('\n').split('\t')
        return {n: i for i, n in enumerate(header)}, rows(p)
    inventory = {}
    ix, it = indexed('inventory.tsv.gz')
    for r in it:
        key = (r[ix['module']], r[ix['site']])
        if key in inventory: raise ValueError('Duplicate topology site')
        inventory[key] = {k: int(r[ix[k]]) for k in ('bel_count', 'bel_pin_count', 'site_pin_count')}
    owners = {}
    for node, kind in rows(occupancy):
        if kind not in ('static', 'boundary'): raise ValueError('Unclassified route owner')
        owners[node] = owners.get(node, 0) | (1 if kind == 'static' else 2)
    attributes = {}
    ix, it = indexed('node_attributes.tsv.gz')
    for r in it:
        n = r[ix['node']]
        if n in attributes: raise ValueError('Duplicate node attribute')
        attributes[n] = int(r[ix['is_gnd']]) | int(r[ix['is_vcc']]) << 1 | int(r[ix['is_bad']]) << 2
    if not set(owners).issubset(attributes): raise ValueError('Owner outside certified topology')
    tick('topology_and_occupancy_load', t)
    t = time.monotonic()
    graph, graph_counts = {}, collections.Counter()
    ix, it = indexed('external_graph.tsv.gz')
    with writer(output/'external_access.tsv.gz', ['node','direction','state','source_owner_kind',
            'ordinary_first_hops','fixed_static_first_hops','fixed_boundary_first_hops',
            'other_first_hops','constant_tie_nodes','bad_nodes','adjacent_nodes']) as f:
        for r in it:
            node, direction, adjacent = (r[ix[k]] for k in ('node', 'direction', 'adjacent_nodes'))
            adj = adjacent.split('|') if adjacent else []
            if node not in attributes: raise ValueError('Missing external source attribute')
            ordinary = [n for n in adj if not attributes[n] & 7]
            ties = [n for n in adj if attributes[n] & 3]
            bad = [n for n in adj if attributes[n] & 4]
            ns = sum(bool(owners.get(n, 0) & 1) for n in ordinary)
            nb = sum(owners.get(n, 0) == 2 for n in ordinary)
            src = 'fixed_static' if owners.get(node, 0) & 1 else ('fixed_boundary' if owners.get(node, 0) & 2 else 'unoccupied')
            if src == 'fixed_static': state = 'SOURCE_NODE_FIXED_STATIC'
            elif not adj: state = 'DEDICATED_OR_NO_ADJACENCY'
            elif not ordinary: state = 'CONSTANT_ONLY_OR_SPECIAL'
            elif ns == len(ordinary): state = 'ALL_ORDINARY_FIRST_HOPS_FIXED_STATIC'
            elif src == 'fixed_boundary' or nb: state = 'BOUNDARY_NET_DEPENDENT'
            elif ns: state = 'PARTIAL_STATIC_FIRST_HOP_BLOCKAGE'
            else: state = 'NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP'
            key = (node, direction)
            if key in graph: raise ValueError('Duplicate graph identity')
            graph[key] = state; graph_counts[state] += 1
            f.write('\t'.join(map(str, [node,direction,state,src,len(ordinary),ns,nb,len(ordinary)-ns-nb,'|'.join(ties),'|'.join(bad),adjacent]))+'\n')
    tick('external_classification', t)
    t = time.monotonic()
    site_states, site_counts = {}, collections.Counter()
    ix, it = indexed('site_pins.tsv.gz')
    with writer(output/'site_pin_access.tsv.gz', ['module','site','site_pin','direction','state','nodes','node_states']) as f:
        for r in it:
            module, site, pin, direction, nodes = (r[ix[k]] for k in ('module','site','site_pin','direction','nodes'))
            states = [graph[(n,direction)] for n in nodes.split('|')] if nodes else []
            state = mapped_state(states, 'site')
            if pin in site_states: raise ValueError('Duplicate site pin')
            site_states[pin] = state; site_counts[(module,site)] += 1
            f.write('\t'.join([module,site,pin,direction,state,nodes,'|'.join(states)])+'\n')
    tick('site_pin_classification', t)
    del graph, attributes
    t = time.monotonic()
    bel_types = {}; bel_counts = collections.Counter()
    ix, it = indexed('bels.tsv.gz')
    for r in it:
        bel = r[ix['bel']]
        if bel in bel_types: raise ValueError('Duplicate BEL')
        bel_types[bel] = r[ix['type']]; bel_counts[(r[ix['module']],r[ix['site']])] += 1
    pin_counts, masks = collections.Counter(), set()
    ix, it = indexed('bel_pins.tsv.gz')
    with writer(output/'bel_pin_access.tsv.gz', ['module','site','bel','bel_type','bel_pin','direction','state','site_pins','site_pin_states']) as f:
        for r in it:
            module, site, pin, direction, pins = (r[ix[k]] for k in ('module','site','bel_pin','direction','site_pins'))
            bel = pin.rsplit('/',1)[0]
            states = [site_states[n] for n in pins.split('|')] if pins else []
            state = mapped_state(states, 'bel')
            typ = bel_types.get(bel, 'NOT_IN_GET_BELS_INVENTORY')
            if typ == 'NOT_IN_GET_BELS_INVENTORY' and pins: raise ValueError('Mapped BEL absent from inventory')
            pin_counts[(module,site)] += 1
            if state == HARD and direction == 'OUT' and pin.endswith('/Q') and re.fullmatch('[A-H]FF2?', bel.rsplit('/',1)[-1]):
                key = (module,site,bel.rsplit('/',1)[-1])
                if key in masks: raise ValueError('Duplicate hard FF Q')
                masks.add(key)
            f.write('\t'.join([module,site,bel,typ,pin,direction,state,pins,'|'.join(states)])+'\n')
    tick('bel_pin_classification', t)
    counts = {}
    for key, expected in inventory.items():
        actual = {'bel_count':bel_counts[key], 'bel_pin_count':pin_counts[key], 'site_pin_count':site_counts[key]}
        if actual != expected: raise ValueError('Incomplete classification at ' + str(key))
        count = counts.setdefault(key[0], collections.Counter())
        count.update(sites=1,bels=actual['bel_count'],bel_pins=actual['bel_pin_count'],site_pins=actual['site_pin_count'])
    if set(pin_counts) != set(inventory) or set(site_counts) != set(inventory): raise ValueError('Site coverage differs')
    (output/'ff_q_mask.tsv').write_text('module\tsite\tbel\n'+''.join('\t'.join(r)+'\n' for r in sorted(masks)))
    result = {'state':'completed', 'seconds':time.monotonic()-start, 'stages':stages,
              'coverage':counts, 'graph_counts':graph_counts, 'hard_ff_rows':sorted(masks),
              'occupancy_sha256':sha(occupancy), 'not_a_full_routability_certificate':True}
    (output/'classification.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--topology',required=True);parser.add_argument('--occupancy',required=True);parser.add_argument('--output',required=True)
    args = parser.parse_args()
    print(json.dumps(classify(args.topology,args.occupancy,args.output)))
