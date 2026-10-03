#!/usr/bin/env python3
"""Create a separate OpenPiton input, removing only byte-identical cell records."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from prepare_amf2_cases import save, sha, now

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bundle',type=Path,required=True)
    args=parser.parse_args()
    bundle=args.bundle.resolve()
    directory=bundle/'cases/openpiton'
    case=json.loads((directory/'case.json').read_text())
    source=Path(case['inputs']['vivado extracted design information file']['snapshot'])
    if sha(source)!=case['inputs']['vivado extracted design information file']['sha256']:
        raise RuntimeError('Source snapshot changed')
    output=directory/'derived-inputs/netlist-unique-cells.zip'
    output.parent.mkdir(parents=True,exist_ok=True)
    if output.exists():raise RuntimeError('Derived file already exists')
    seen={}
    removed=[]
    with zipfile.ZipFile(source) as src, zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as dst:
        names=[n for n in src.namelist() if not n.endswith('/')]
        if len(names)!=1:raise RuntimeError('Expected single netlist member')
        with src.open(names[0]) as stream, dst.open(names[0],'w',force_zip64=True) as writer:
            block=[]
            name=None
            def emit():
                if not block:return
                data=b''.join(block)
                if name is None:
                    writer.write(data);return
                digest=hashlib.sha256(data).hexdigest()
                if name in seen:
                    if seen[name]!=digest:raise RuntimeError('Different records share a name: '+name)
                    removed.append({'cell':name,'block_sha256':digest,'bytes':len(data)})
                else:
                    seen[name]=digest
                    writer.write(data)
            for line in stream:
                if line.startswith(b'curCell=> '):
                    emit()
                    name=line.decode().strip()[10:].rsplit(' type=> ',1)[0]
                    block=[]
                block.append(line)
            emit()
    if len(removed)!=12 or len(seen)!=309133:raise RuntimeError('Unexpected duplicate counts')
    with zipfile.ZipFile(output) as z:
        if z.testzip() is not None:raise RuntimeError('Derived ZIP CRC failure')
    config=json.loads((directory/'inspect_config.json').read_text())
    config['vivado extracted design information file']=str(output)
    save(directory/'inspect_unique_config.json',config)
    result={'created_at':now(),'source':str(source),'source_sha256':sha(source),'derived':str(output),
            'derived_sha256':sha(output),'removed_byte_identical_records':removed,'unique_cells':len(seen),
            'all_retained_cell_blocks_unchanged':True,'original_preserved':True,
            'placement_executed':False,'amf_loader_validated':False,'clock_coverage_repaired':False}
    save(directory/'deduplication.json',result)
    print(json.dumps({k:result[k] for k in ['derived','derived_sha256','unique_cells','original_preserved']},indent=2))

if __name__=='__main__':
    main()
