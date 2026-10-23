#!/usr/bin/env python3
"""Fix inherited NUL bytes in packed filename digits in a separate RTL copy."""
import difflib
import json
from pathlib import Path
import re
import shutil
import sys
from prepare_amf2_u250_cores import sha

root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve()
old=base/'memn2n-retry03'
new=base/'memn2n-retry06'
new.mkdir(exist_ok=False)
(new/'ip').mkdir()
for p in old.iterdir():
    if p.suffix in ('.v','.xci') or p.name == 'core_clock.xdc':
        shutil.copy2(p,new/p.name)
rtl=next((root/'data/reference/amf2-cases-20260930/projects/memn2n').rglob('sources_1/new'))
memory_inputs=new/'mem-inputs'
memory_inputs.mkdir()
for p in (rtl.parent/'mem').glob('*.mem'):
    shutil.copy2(p,memory_inputs/p.name)
derived=new/'derived-rtl'
derived.mkdir()
changes=[]
diff=[]
for p in rtl.iterdir():
    if p.suffix not in ('.v','.h'):continue
    original=p.read_text()
    lines=[]
    replacements=0
    for line in original.splitlines(keepends=True):
        if re.search(r'\((\w+)\s*([/%])\s*10\)\s*\+\s*48',line):
            assert '.mem' in line, (p,line)
            line,n=re.subn(r'\((\w+)\s*([/%])\s*10\)\s*\+\s*48',r"8'((\1\2 10)+48)",line)
            replacements+=n
        lines.append(line)
    result=''.join(lines)
    (derived/p.name).write_text(result)
    if replacements:
        changes.append(dict(file=p.name,replacements=replacements,original_sha256=sha(p),derived_sha256=sha(derived/p.name)))
        diff.extend(difflib.unified_diff(original.splitlines(True),result.splitlines(True),fromfile=str(p),tofile=str(derived/p.name)))
assert sum(c['replacements'] for c in changes)==10, changes
for prefix in ('w_emb_a_init_q7_24_','w_emb_c_init_q7_24_','w_emb_q_init_q7_24_','w_fc_init_q7_24_','w_fc_w_sm_init_q7_24_'):
    for i in range(32):assert (memory_inputs/f'{prefix}{i:02}.mem').is_file()
script=(old/'synthesize.tcl').read_text().replace(str(old),str(new)).replace(str(rtl),str(derived))
for p in memory_inputs.glob('*.mem'):
    script=script.replace(str(new/p.name),str(p))
script=script.replace('read_verilog [list','read_verilog -sv [list')
script=script.replace('synth_design -top amf2_memn2n_core','set_property verilog_define {Q_FORM_Q_7_24} [current_fileset]\nset_msg_config -id {Synth 8-4445} -new_severity ERROR\nsynth_design -top amf2_memn2n_core')
(new/'synthesize.tcl').write_text(script)
(new/'rom_filename_fix.patch').write_text(''.join(diff))
manifest=json.loads((old/'input_manifest.json').read_text())
manifest.update(previous_attempt=str(old),rom_filename_fix=changes,
                rom_policy='Digit characters are explicitly 8 bits; preserve supplied ROM values. Missing readmem files are fatal.',
                additional_verilog_define='Q_FORM_Q_7_24: selects only three embedding filename prefixes; matches existing BW_IWL=7 and 32-bit data',
                source_mappings=[dict(original=str(rtl/p.name),resolved=str(p),sha256=sha(p)) for p in derived.iterdir()],
                memory_files=[dict(file=str(p),sha256=sha(p)) for p in memory_inputs.glob('*.mem')])
(new/'input_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
items=json.loads((base/'catalog-v9.json').read_text())
for i in items:
    if i['case']=='memn2n':i['directory']=str(new)
(base/'catalog-v10.json').write_text(json.dumps(items,indent=2)+'\n')
print(new)
