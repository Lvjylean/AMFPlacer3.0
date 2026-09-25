#!/usr/bin/env python3
"""Bounded diagnostics on eda072; never modify the original run or source.

Uses ABI offsets checked against the preserved binary's disassembly and headers.
Only valid for the faceDetect-20260925-131037 binary snapshot.
"""
import collections
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2] / 'experiments/runs/faceDetect-20260925-131037'
DIAG = ROOT / 'diagnostics/macro-conflict-134719'


def debug(name, commands, config):
    script = DIAG / (name + '.gdb')
    script.write_text(commands)
    result = subprocess.run(
        ['gdb', '-batch', '-x', str(script), '--args',
         str(ROOT / 'bin/AMFPlacer'), str(config)],
        cwd=DIAG, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=90)
    (DIAG / (name + '.log')).write_text(result.stdout)
    print('DEBUG', name, 'EXIT', result.returncode)
    for line in result.stdout.splitlines():
        if line.startswith(('DIAG_', 'Breakpoint ', '#0 ', '#1 ', 'Traceback', 'Error')):
            print(line[:1200])
    if result.returncode:
        raise RuntimeError(result.stdout[-2500:])
    return result.stdout


manifest = json.loads((ROOT / 'manifest.json').read_text())
binary_hash = hashlib.sha256((ROOT / 'bin/AMFPlacer').read_bytes()).hexdigest()
assert binary_hash == manifest['AMFPlacer_sha256']

debug('native-macros', '''set pagination off
set confirm off
break _ZN13InitialPacker18loadOtherCLBMacrosENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
run
python
import gdb, struct, json
from pathlib import Path
base=Path.cwd()
selected=set((base / 'selected-cells.txt').read_text().splitlines())
mem=gdb.selected_inferior()
def u64(p): return struct.unpack('<Q', mem.read_memory(p,8))[0]
def i32(p): return struct.unpack('<i', mem.read_memory(p,4))[0]
def name(p): return gdb.Value(u64(p)).cast(gdb.lookup_type('char').pointer()).string()
packer=int(gdb.parse_and_eval('$rdi'))
cellset=u64(packer+64); cellmap=u64(packer+72)
stack=[u64(cellset+16)]; found=[]
while stack:
 node=stack.pop()
 if not node: continue
 stack.extend([u64(node+16),u64(node+24)])
 cell=u64(node+32); cn=name(cell+8)
 if cn not in selected: continue
 cid=i32(cell+52); mn=u64(cellmap+16)
 while mn and i32(mn+32)!=cid: mn=u64(mn+(16 if cid<i32(mn+32) else 24))
 pu=u64(mn+40)
 found.append({'cell':cn,'cell_id':cid,'existing_macro':name(pu+56)})
found.sort(key=lambda x:x['cell'])
(base/'native-macro-conflicts.json').write_text(json.dumps(found,indent=2)+'\\n')
print('DIAG_CONFLICT_COUNT='+str(len(found)))
print('DIAG_FIRST_CONFLICT='+json.dumps(found[0] if found else None))
end
quit
''', DIAG / 'config.json')

conflicts = json.loads((DIAG / 'native-macro-conflicts.json').read_text())
conflicting_names = {row['cell'] for row in conflicts}
property_rows = [line.split('\t') for line in
                 (DIAG / 'cell-properties.tsv').read_text().splitlines()]
assert len(property_rows) == 797
groups = collections.Counter(tuple(row[1:5]) for row in property_rows)
conflict_groups = collections.Counter(tuple(row[1:5]) for row in property_rows
                                      if row[0] in conflicting_names)
print('PROPERTY_GROUPS', groups)
print('CONFLICT_PROPERTY_GROUPS', conflict_groups)

original = (ROOT / 'inputs/exported/faceDetect_unpredictableMacros').read_text().splitlines()
deduplicated = list(dict.fromkeys(original))
assert all(line.startswith('name=> ') for line in original)
variants = {
    'deduplicated': deduplicated,
    'without_native_overlap': [line for line in deduplicated
                               if line.split()[1] not in conflicting_names],
}
results = {}
for variant, rows in variants.items():
    # Keep the original header convention for a controlled comparison.
    # This diagnostic is not a production repair of constraints.
    macro_file = DIAG / (variant + '.macros')
    macro_file.write_text('\n'.join(rows) + '\n')
    config = json.loads((DIAG / 'config.json').read_text())
    config['unpredictable macro file'] = str(macro_file)
    config_file = DIAG / (variant + '.json')
    config_file.write_text(json.dumps(config, indent=2) + '\n')
    output = debug(variant, '''set pagination off
set confirm off
set breakpoint pending on
break __assert_fail
break _ZN13InitialPacker16findLUTRAMMacrosEv
run
bt 4
quit
''', config_file)
    results[variant] = {
        'rows': len(rows),
        'passed_other_clb_macros': 'Breakpoint 2,' in output,
        'hit_assertion': 'Breakpoint 1,' in output,
    }

summary = {
    'original_macro_rows': len(original),
    'unique_macro_rows': len(deduplicated),
    'native_conflicts': len(conflicts),
    'existing_macros': sorted({row['existing_macro'] for row in conflicts}),
    'property_columns': ['REF_NAME', 'KEEP', 'ASYNC_REG', 'XPM_CDC'],
    'property_groups': [{'values': list(k), 'count': v} for k, v in groups.items()],
    'conflict_property_groups': [{'values': list(k), 'count': v} for k, v in conflict_groups.items()],
    'controlled_runs': results,
    'note': 'All diagnostic runs stopped before global placement; original source and production run untouched.',
}
(DIAG / 'diagnosis.json').write_text(json.dumps(summary, indent=2) + '\n')
print('DIAG_SUMMARY', json.dumps(summary))
