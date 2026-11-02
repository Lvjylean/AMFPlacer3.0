"""DCP inventory policies for full designs; no case-specific hierarchy names."""
from collections import defaultdict, deque
import csv
import json
from pathlib import Path

SITE_ONLY = frozenset(('IBUF IBUFDS IBUF_ANALOG OBUF OBUFT IOBUF IOBUFE3 IOBUFDS '
                       'OBUFDS_DUAL_BUF BUFGCE').split())
FIXED_LEAF = frozenset(('IBUFDS_GTE3 IBUFDS_GTE4 BUFGCE_DIV BUFGCTRL BUFG_GT BUFG_GT_SYNC '
                        'MMCME3_ADV MMCME4_ADV PLLE3_ADV PLLE4_ADV GTHE3_CHANNEL GTHE3_COMMON '
                        'GTYE4_CHANNEL GTYE4_COMMON PCIE_3_1 PCIE40E4 RXTX_BITSLICE '
                        'BITSLICE_CONTROL TX_BITSLICE_TRI OSERDESE3 RIU_OR HPIO_VREF BSCANE2 SYSMONE1').split())


def async_groups(rows, limit=16):
    if not 1 <= limit <= 16:
        raise ValueError('Invalid SLICE FF limit')
    cells = {r['cell']: r for r in rows}
    if len(cells) != len(rows):
        raise ValueError('Duplicate ASYNC_REG cell')
    adjacency = defaultdict(set)
    incoming = defaultdict(set)
    edges, excluded = [], []
    def clock(r):
        return r['clock_driver'], r['clock_inverted']
    def controls(r):
        return clock(r) + (r['primitive'], r['ce_driver'], r['sr_driver'], r['control_polarities'])
    for name, row in cells.items():
        if row['primitive'] not in {'FDRE', 'FDSE', 'FDCE', 'FDPE'}:
            raise ValueError('Unsupported synchronizer primitive: ' + name)
        if not row['clock_driver'] or ',' in row['clock_driver']:
            raise ValueError('Missing/ambiguous synchronizer clock: ' + name)
        driver = row['d_driver']
        if not driver.endswith('/Q') or driver[:-2] not in cells:
            continue  # Combinational logic terminates a direct synchronization chain.
        source = driver[:-2]
        if clock(cells[source]) != clock(row):
            excluded.append((source, name))
            continue
        edges.append((source, name))
        if controls(cells[source]) == controls(row):
            adjacency[source].add(name)
            adjacency[name].add(source)
            incoming[name].add(source)
    pending = set(cells)
    groups = []
    # Start from chain roots; a deterministic fallback also handles cycles.
    roots = sorted(n for n in cells if not incoming[n]) + sorted(cells)
    for root in roots:
        if root not in pending:
            continue
        queue = deque([root])
        while queue:
            seed = queue.popleft()
            if seed not in pending:
                continue
            group, frontier = [], deque([seed])
            while frontier and len(group) < limit:
                name = frontier.popleft()
                if name not in pending:
                    continue
                pending.remove(name)
                group.append(name)
                frontier.extend(sorted(adjacency[name] & pending))
            queue.extend(frontier)
            groups.append(group)
    owner = {n: i for i, g in enumerate(groups) for n in g}
    proximity = [(a, b) for a, b in edges if owner[a] != owner[b]]
    return dict(groups=groups, proximity_edges=proximity, excluded_cross_clock_edges=excluded,
                async_cells=len(cells), direct_same_clock_edges=len(edges),
                packing_policy='same-SLICE compatible groups, at most 16 FFs; spare LUTs remain usable',
                split_policy='control differences or capacity; cross-group Q-to-D proximity is a soft objective')


def write_async_metadata(inventory, output):
    with (inventory/'async_regs.tsv').open() as f:
        result = async_groups(list(csv.DictReader(f, delimiter='\t')))
    (output/'async_groups.txt').write_text('AMF_ASYNC_GROUPS 1\n' + ''.join(
        f'async_{i} {name}\n' for i, group in enumerate(result['groups']) for name in group))
    (output/'async_proximity.txt').write_text(''.join(f'{a} {b}\n' for a, b in result['proximity_edges']))
    (output/'async_grouping.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def validate_binary(config, binary):
    if config.get('CompositeIOTimingBoundary') != 'true' and not config.get('ASYNC_REG groups file') and not config.get('FF control polarities file'):
        return
    import subprocess
    caps = json.loads(subprocess.check_output([str(binary), '--capabilities'], text=True, timeout=15))
    if config.get('FF control polarities file') and caps.get('ff_control_polarity_schema') != 1:
        raise ValueError('Binary lacks FF control polarity compatibility')
    if config.get('CompositeIOTimingBoundary') == 'true' and not config.get('FF control polarities file'):
        raise ValueError('Full-interface input requires complete FF control polarity metadata')
    if caps.get('full_interface_adaptation_schema') != 1:
        raise ValueError('Binary lacks full interface/synchronizer adaptation')


def validate_ff_binding(config, record):
    if not config.get('FF control polarities file'):
        return
    import hashlib
    path = Path(config['FF control polarities file'])
    if not record or record.get('ff_control_polarities', {}).get('sha256') != hashlib.sha256(path.read_bytes()).hexdigest():
        raise ValueError('FF control metadata is not bound to this input provenance')
