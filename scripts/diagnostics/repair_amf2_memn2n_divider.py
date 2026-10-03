#!/usr/bin/env python3
"""Regenerate the release divider IP for U250 with all original PARAM_VALUEs."""
import json
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from prepare_amf2_u250_cores import tq, sha

root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve()
old=base/'memn2n'
new=base/'memn2n-retry03'
new.mkdir(exist_ok=False)
(new/'ip').mkdir()
for p in old.iterdir():
    if p.suffix in ('.v','.xdc','.mem'):
        shutil.copy2(p,new/p.name)
archive=next((root/'data/reference/amf2-cases-20260930/projects/memn2n').rglob('div_32.xcix'))
with zipfile.ZipFile(archive) as z:
    xci=z.read('div_32/div_32.xci')
(new/'original_div_32.xci').write_bytes(xci)
params={}
for element in ET.fromstring(xci).iter():
    if element.tag.endswith('configurableElementValue'):
        key=next((v for k,v in element.attrib.items() if k.endswith('referenceId')), '')
        if key.startswith('PARAM_VALUE.'):
            params[key.removeprefix('PARAM_VALUE.')]=element.text or ''
commands=['create_ip -name div_gen -vendor xilinx.com -library ip -version 5.1 -module_name div_32 -dir '+tq(new/'ip')]
commands.append('set_property -dict [list '+' '.join(tq('CONFIG.'+k)+' '+tq(v) for k,v in params.items() if k!='Component_Name')+'] [get_ips div_32]')
commands.extend(['report_property [get_ips div_32] -file '+tq(new/'divider_properties.rpt'),
                 'generate_target all [get_ips div_32]', 'synth_ip [get_ips div_32]'])
script=(old/'synthesize.tcl').read_text().replace(str(old),str(new))
idx=script.index('read_verilog ')
script=script[:idx]+'\n'.join(commands)+'\n'+script[idx:]
(new/'synthesize.tcl').write_text(script)
manifest=json.loads((old/'input_manifest.json').read_text())
manifest.update(previous_attempt=str(old),divider_source_archive=str(archive),divider_source_archive_sha256=sha(archive),
                divider_parameters=params,divider_policy='Regenerate div_gen 5.1 for U250; preserve every original configurable PARAM_VALUE',
                generated_wrapper_sha256=sha(new/'amf2_memn2n_core.v'))
(new/'input_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
items=json.loads((base/'catalog-v4.json').read_text())
for item in items:
    if item['case']=='memn2n':item['directory']=str(new)
(base/'catalog-v6.json').write_text(json.dumps(items,indent=2)+'\n')
print(new)
