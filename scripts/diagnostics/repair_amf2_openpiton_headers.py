#!/usr/bin/env python3
"""Supplement release-archive omissions from pinned official OpenPiton blobs."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import urllib.request

base = Path(sys.argv[1]).resolve()
old = base/'openpiton'
new = base/'openpiton-retry03'
assert json.loads((old/'synthesis_status.json').read_text())['exit_code'] != 0
new.mkdir(exist_ok=False)
headers = new/'upstream-headers'
headers.mkdir()
commit = 'aeb8e684de51f328ba830de0f2762e8494bcc5d0'
expected = {'iop.h':'b92ec74b00cb0ae5a4b8e9c0b07e4b3b3420adeb',
            'sys.h':'d7c01b59503708f47c3118e4b3eef16ebb63b79d',
            'tlu.h':'24ca7a86b815f2b9114d56edfd2a846866b65b56'}
records = []
for name, git_blob in expected.items():
    url = f'https://raw.githubusercontent.com/PrincetonUniversity/openpiton/{commit}/piton/design/include/{name}'
    data = urllib.request.urlopen(url, timeout=60).read()
    actual = hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()
    assert actual == git_blob, (name, actual)
    (headers/name).write_bytes(data)
    records.append(dict(file=str(headers/name), url=url, commit=commit, git_blob=git_blob,
                        sha256=hashlib.sha256(data).hexdigest()))
for name in ('amf2_openpiton_core.sv', 'core_clock.xdc'):
    shutil.copy2(old/name, new/name)
script = (old/'synthesize.tcl').read_text().replace(str(old),str(new))
needle = 'synth_design -top'
idx = script.index(needle)
script = (script[:idx] + 'set_property include_dirs [concat [get_property include_dirs [current_fileset]] {'
          + str(headers) + '}] [current_fileset]\n' + script[idx:])
(new/'synthesize.tcl').write_text(script)
manifest = json.loads((old/'input_manifest.json').read_text())
manifest.update(previous_attempt=str(old), header_supplements=records,
                repair='Three missing includes; no original RTL or compile definitions changed')
(new/'input_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
items = json.loads((base/'catalog.json').read_text())
for item in items:
    if item['case']=='openpiton':
        item['directory']=str(new)
(base/'catalog-v3.json').write_text(json.dumps(items,indent=2)+'\n')
print(new)
