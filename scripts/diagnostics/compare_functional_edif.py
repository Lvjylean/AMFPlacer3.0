#!/usr/bin/env python3
"""Complete hierarchical EDIF ledger comparison, not a SAT/formal-equivalence tool.

All properties and syntax are retained. Status timestamps are separated as metadata.
Object/endpoint ordering is normalized; identifiers and renamed display names are
both retained, so an identifier change fails conservatively rather than hiding a diff.
No function-stripping or property whitelist is applied when exporting the ledger.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import sqlite3

TOKEN = re.compile(r'"[^\"]*"|[()]|[^\s()\"]+')


def name_of(x):
    if isinstance(x, str):
        return x
    if x[0].lower() == 'rename':
        return x[2]
    if x[0].lower() == 'array':
        return name_of(x[1])
    raise ValueError(f'Unknown EDIF declaration: {x}')


def encode(x):
    return json.dumps(x, ensure_ascii=False, separators=(',', ':'))


def canonical(x):
    if not isinstance(x, list):
        return x
    if not x or not isinstance(x[0], str):
        raise ValueError('Malformed EDIF expression')
    kind = x[0].lower()
    values = [canonical(v) for v in x[1:]]
    # These child lists are sets in the structural EDIF emitted by Vivado.
    if kind in {'joined', 'interface', 'contents'}:
        values.sort(key=encode)
    elif kind in {'instance', 'net', 'port', 'cell', 'view', 'library', 'edif', 'design'}:
        # Declaration name is positional; attributes/properties are unordered.
        values = values[:1] + sorted(values[1:], key=encode)
    return [kind] + values


def export_ledger(source, database):
    if database.exists():
        raise FileExistsError(database)
    con = sqlite3.connect(database)
    con.execute('create table objects(scope text, kind text, name text, digest text, value text, primary key(scope,kind,name)) without rowid')
    counts = collections.Counter()
    syntax = collections.Counter()
    props = collections.Counter()
    stack = []
    rawhash = hashlib.sha256()

    def add(scope, kind, name, node):
        value = encode(canonical(node))
        con.execute('insert into objects values(?,?,?,?,?)',
                    (scope, kind, name, hashlib.sha256(value.encode()).hexdigest(), value))
        counts[kind] += 1

    with source.open() as f:
        for lineno, line in enumerate(f, 1):
            rawhash.update(line.encode())
            tokens = TOKEN.findall(line)
            # Vivado EDIF strings are single-line. Fail closed on unsupported syntax.
            if TOKEN.sub('', line).strip():
                raise ValueError(f'Unparsed token/string at line {lineno}')
            for token in tokens:
                if token == '(':
                    stack.append([])
                elif token == ')':
                    if not stack:
                        raise ValueError(f'Unbalanced close at {lineno}')
                    node = stack.pop()
                    if not node:
                        raise ValueError(f'Empty expression at {lineno}')
                    kind = node[0].lower()
                    syntax[kind] += 1
                    if kind == 'property':
                        props[name_of(node[1])] += 1
                    # Lift every instance, net, port, and cell into its own ledger row.
                    if kind in {'instance', 'net', 'port', 'cell'}:
                        ancestors = [n for n in stack if n and isinstance(n[0], str) and n[0].lower() in {'library','external','cell','view'}]
                        scope = '/'.join(name_of(n[1]) for n in ancestors)
                        add(scope, kind, name_of(node[1]), node)
                        node = ['auditObjectRef', kind, node[1]]
                    if kind == 'status':
                        add('', 'status', str(counts['status']), node)
                        node = ['auditStatusRef']
                    if stack:
                        stack[-1].append(node)
                    else:
                        if kind != 'edif':
                            raise ValueError('Not an EDIF root')
                        add('', 'root', name_of(node[1]), node)
                else:
                    if not stack:
                        raise ValueError(f'Atom outside expression at {lineno}')
                    stack[-1].append(token)
            if lineno % 1000000 == 0:
                con.commit()
                print(f'{source.name}: {lineno} lines, {sum(counts.values())} objects', flush=True)
    if stack or counts['root'] != 1:
        raise ValueError('Incomplete or multiple EDIF roots')
    con.commit()
    digest = hashlib.sha256()
    for row in con.execute("select scope,kind,name,digest from objects where kind!='status' order by scope,kind,name"):
        digest.update((encode(row)+'\n').encode())
    con.close()
    result = dict(source=str(source), raw_sha256=rawhash.hexdigest(), structural_sha256=digest.hexdigest(),
                  object_counts=dict(counts), syntax_counts=dict(syntax), property_counts=dict(props),
                  normalization='unordered declarations/properties/joined endpoints; status excluded; ALL properties retained')
    database.with_suffix('.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k:v for k,v in result.items() if k not in {'syntax_counts','property_counts'}}, indent=2))


def compare(left, right, output):
    con = sqlite3.connect(left)
    con.execute('attach database ? as other', (str(right),))
    counts = collections.Counter()
    with output.open('w') as f:
        for category, query in [
            ('changed', "select a.scope,a.kind,a.name,a.value,b.value from objects a join other.objects b using(scope,kind,name) where a.digest!=b.digest"),
            ('removed', "select a.scope,a.kind,a.name,a.value,null from objects a left join other.objects b using(scope,kind,name) where b.name is null"),
            ('added', "select b.scope,b.kind,b.name,null,b.value from other.objects b left join objects a using(scope,kind,name) where a.name is null")]:
            for scope, kind, name, before, after in con.execute(query):
                counts[f'{category}:{kind}'] += 1
                f.write(encode(dict(change=category,scope=scope,kind=kind,name=name,
                                    before=json.loads(before) if before else None,
                                    after=json.loads(after) if after else None))+'\n')
    result = dict(left=str(left),right=str(right),differences=dict(counts),
                  exact_structure_and_all_properties_equal=not any(v for k,v in counts.items() if not k.endswith(':status')))
    output.with_suffix('.summary.json').write_text(json.dumps(result,indent=2))
    con.close()
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest='command',required=True)
    e = sub.add_parser('export'); e.add_argument('edif',type=Path); e.add_argument('database',type=Path)
    c = sub.add_parser('compare'); c.add_argument('left',type=Path); c.add_argument('right',type=Path); c.add_argument('output',type=Path)
    args = p.parse_args()
    if args.command == 'export': export_ledger(args.edif,args.database)
    else: compare(args.left,args.right,args.output)
