#!/usr/bin/env python3
"""Conservative structural comparison of Vivado funcsim exports, not formal LEC.

Ignore comments/attributes and ordering of named parameter/port associations.
Never decrypt protected blocks or assume unmatched/changed logic is equivalent.
Only accept the structural syntax actually handled below; unknown syntax fails.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import sys
import time

TOKEN = re.compile(r'\\\S+|"(?:\\.|[^"\\])*"|[A-Za-z_$][\w$]*|[^\s]')
ATTR = re.compile(r'\(\*.*?\*\)', re.S)


def group(tokens, index):
    if tokens[index] != '(':
        raise ValueError('Expected parenthesis: ' + str(tokens[index:index+6]))
    start = index + 1
    depth = 1
    for index in range(start, len(tokens)):
        depth += (tokens[index] == '(') - (tokens[index] == ')')
        if depth == 0:
            return tokens[start:index], index + 1
    raise ValueError('Unclosed parenthesis')


def associations(tokens):
    result = {}
    index = 0
    while index < len(tokens):
        if tokens[index] != '.':
            raise ValueError('Expected named association: ' + str(tokens[index:index+8]))
        name = tokens[index + 1]
        value, index = group(tokens, index + 2)
        if name in result:
            raise ValueError('Duplicate association: ' + name)
        result[name] = tuple(value)
        if index < len(tokens):
            if tokens[index] != ',':
                raise ValueError('Missing association comma')
            index += 1
    return tuple(sorted(result.items()))


def lines(path):
    with path.open() as stream:
        yield from stream


def parse(path):
    cells = {}
    modules = {}
    module = None
    buffer = []
    protected = False
    encrypted_blocks = 0
    global_tail = []
    counts = collections.Counter()
    for line_number, line in enumerate(lines(path), 1):
        if line.strip() == '`ifndef GLBL' or global_tail:
            if buffer or module is not None:
                raise ValueError('Global simulation tail inside design module')
            global_tail.append(line)
            continue
        if 'begin_protected' in line:
            if buffer:
                raise ValueError('Pending statement before protected block')
            protected = True
            encrypted_blocks += 1
        if protected:
            if 'end_protected' in line:
                protected = False
            continue
        if line.lstrip().startswith('//') or not line.strip():
            continue
        if line.lstrip().startswith('`timescale'):
            continue
        if line.strip() == 'endmodule':
            if buffer:
                raise ValueError('Pending statement at endmodule')
            module = None
            continue
        buffer.append(line)
        if not line.rstrip().endswith(';'):
            continue
        statement = ATTR.sub('', ''.join(buffer)).strip()
        buffer = []
        tokens = TOKEN.findall(statement)
        if not tokens or tokens[-1] != ';':
            raise ValueError('Malformed statement at ' + str(line_number))
        tokens.pop()
        keyword = tokens[0]
        counts[keyword] += 1
        if keyword == 'module':
            module = tokens[1]
            if module in modules:
                raise ValueError('Duplicate module: ' + module)
            modules[module] = {'header': tuple(tokens[2:]), 'ports': [], 'assigns': []}
        elif keyword in ('input', 'output', 'inout'):
            modules[module]['ports'].append(tuple(tokens))
        elif keyword == 'wire':
            pass
        elif keyword == 'assign':
            modules[module]['assigns'].append(tuple(tokens))
        else:
            if module is None:
                raise ValueError('Instance outside module at line ' + str(line_number) + ': ' + statement[:100])
            index = 1
            params = ()
            if tokens[index] == '#':
                values, index = group(tokens, index + 1)
                params = associations(values)
            name = tokens[index]
            values, index = group(tokens, index + 1)
            if index != len(tokens):
                raise ValueError('Unexpected trailing syntax: ' + str(tokens[index:]))
            ports = associations(values)
            key = (module, name)
            if key in cells:
                raise ValueError('Duplicate instance: ' + str(key))
            cells[key] = (keyword, params, ports)
    if buffer or protected or module is not None:
        raise ValueError('Truncated/incomplete export')
    if global_tail and (global_tail[-1].strip() != '`endif' or 'module glbl ();' not in ''.join(global_tail)):
        raise ValueError('Unexpected global simulation tail')
    for item in modules.values():
        item['ports'].sort()
        item['assigns'].sort()
    return cells, modules, {'instances': len(cells), 'modules': len(modules),
                            'encrypted_blocks': encrypted_blocks, 'statement_counts': dict(counts),
                            'global_simulation_tail_sha256': hashlib.sha256(''.join(global_tail).encode()).hexdigest()}


def compare(left, right, output):
    a, am, _ = left
    b, bm, _ = right
    stats = collections.Counter()
    kernel = collections.Counter()
    examples = []
    with output.open('w') as f:
        for key in sorted(a.keys() | b.keys()):
            av, bv = a.get(key), b.get(key)
            categories = []
            if av is None:
                categories.append('added_instance')
            elif bv is None:
                categories.append('removed_instance')
            elif av == bv:
                categories.append('identical_instance')
            else:
                if av[0] != bv[0]: categories.append('type_changed')
                if av[1] != bv[1]: categories.append('parameters_changed')
                if av[2] != bv[2]: categories.append('connections_changed')
                categories.append('changed_instance')
            stats.update(categories)
            if 'device_chain_kernel' in key[1]: kernel.update(categories)
            if categories == ['identical_instance']:
                continue
            item = dict(module=key[0], instance=key[1], categories=categories, left=av, right=bv)
            f.write(json.dumps(item) + '\n')
            if len(examples) < 8: examples.append(item)
    module_differences = [m for m in sorted(am.keys() | bm.keys()) if am.get(m) != bm.get(m)]
    identical_visible_structure = (not (stats['added_instance'] or stats['removed_instance'] or stats['changed_instance'])
                                   and not module_differences)
    return dict(counts=dict(stats), kernel_counts=dict(kernel), module_interface_or_assign_differences=module_differences,
                identical_visible_structure=identical_visible_structure,
                formal_equivalence_proven=False, mismatch_examples=examples,
                differences_server_file=str(output))


def main():
    root = Path(sys.argv[1]).resolve()
    started = time.monotonic()
    parsed = {}
    for label in ('input', 'amf3', 'native'):
        parsed[label] = parse(root / label / 'functional.v')
        print(label, parsed[label][2]['instances'], 'instances parsed', flush=True)
    report = dict(scope='Visible functional-netlist instance types, named parameters and connections; module ports/assigns',
                  limitations=['Encrypted module bodies excluded and not assumed equivalent',
                               'Net aliases, buffer insertion, replication, LUT pin permutations are not semantically reduced',
                               'Differences are unresolved structural differences, not established functional failures',
                               'No stimulus simulation, SDF simulation, board test, SAT or sequential equivalence was run'],
                  inventory={label:data[2] for label,data in parsed.items()}, comparisons={})
    for left, right in [('input', 'amf3'), ('input', 'native'), ('amf3', 'native')]:
        label = left + '_vs_' + right
        report['comparisons'][label] = compare(parsed[left], parsed[right], root / (label + '_differences.jsonl'))
    report['ports_identical'] = len({(root / label / 'ports.tsv').read_bytes() for label in parsed}) == 1
    report['clocks_identical'] = len({(root / label / 'clocks.tsv').read_bytes() for label in parsed}) == 1
    report['global_simulation_tail_identical'] = len({data[2]['global_simulation_tail_sha256'] for data in parsed.values()}) == 1
    report['blackboxes'] = {label:dict(line.split('\t',1) for line in (root/label/'metadata.tsv').read_text().splitlines())['blackboxes'] for label in parsed}
    report['elapsed_seconds'] = time.monotonic() - started
    report['comparison_script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    (root / 'structural_comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v['counts'] for k,v in report['comparisons'].items()}, indent=2))


if __name__ == '__main__':
    main()
