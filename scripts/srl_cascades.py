"""Validate Q31 exits and migrate the old standalone H6LUT export in-place."""
import collections


def correct_and_validate(assignments, kinds, edges):
    result = dict(assignments)
    occupants = collections.defaultdict(list)
    for cell, target in result.items():
        occupants[target.split('/')[0]].append(cell)
    corrections = []
    for source in sorted({a for a, _ in edges}):
        if kinds.get(source) != 'SRLC32E' or source not in result:
            raise ValueError('Missing/unsupported SRL cascade source: ' + source)
        site, _, bel = result[source].partition('/')
        if bel == 'H6LUT' and occupants[site] == [source]:
            before = result[source]
            result[source] = site + '/A6LUT'
            corrections.append(dict(cell=source, before=before, after=result[source],
                                    reason='Standalone Q31 requires the LUT A cascade output'))
    for source, sink in edges:
        if sink not in result:
            raise ValueError('Missing SRL cascade sink: ' + sink)
        a, _, ab = result[source].partition('/')
        b, _, bb = result[sink].partition('/')
        if ab == 'A6LUT':
            continue  # LUT A exposes MC31 to ordinary routing, including another SLICEM.
        if (ab not in [x+'6LUT' for x in 'BCDEFGH'] or a != b
                or bb != chr(ord(ab[0])-1)+'6LUT'):
            raise ValueError('Illegal dedicated SRL cascade: ' + source + ' -> ' + sink)
    return result, corrections
