#!/usr/bin/env python3
"""Safely expand all official project members and verify their ZIP CRCs."""
import argparse
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import time
import zipfile
import zlib
from prepare_amf2_cases import now, save, sha

def crc(path):
    value=0
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):
            value=zlib.crc32(block,value)
    return value & 0xffffffff

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bundle',type=Path,required=True)
    args=parser.parse_args()
    bundle=args.bundle.resolve()
    catalog=json.loads((bundle/'catalog.json').read_text())
    start=time.monotonic()
    manifest={'started_at':now(),'state':'running','script_sha256':sha(Path(__file__)),'projects':[]}
    save(bundle/'expansion.json',manifest)
    for case in catalog:
        if 'archive' not in case: continue
        archive=Path(case['archive']['path'])
        if sha(archive)!=case['archive']['sha256']:
            raise RuntimeError('Archive hash changed')
        destroot=bundle/'projects'/case['name']
        count=total=0
        with zipfile.ZipFile(archive) as package:
            expected=sum(x.file_size for x in package.infolist())
            if shutil.disk_usage(bundle).free < expected+1024**3:
                raise RuntimeError('Insufficient free space')
            for item in package.infolist():
                path=PurePosixPath(item.filename)
                if path.is_absolute() or '..' in path.parts or stat.S_ISLNK(item.external_attr >> 16):
                    raise RuntimeError('Unsafe archive member: '+item.filename)
                dest=destroot/path
                if not dest.resolve().is_relative_to(destroot.resolve()):
                    raise RuntimeError('Unsafe extraction destination')
                if item.is_dir():
                    dest.mkdir(parents=True,exist_ok=True)
                    continue
                dest.parent.mkdir(parents=True,exist_ok=True)
                if dest.exists():
                    if dest.stat().st_size!=item.file_size or crc(dest)!=item.CRC:
                        raise RuntimeError('Existing file differs: '+str(dest))
                else:
                    partial=dest.with_name(dest.name+'.amf2-extract-part')
                    with package.open(item) as source, partial.open('xb') as output:
                        shutil.copyfileobj(source,output,8*1024*1024)
                    partial.rename(dest)
                count+=1;total+=item.file_size
        record={'case':case['name'],'file_count':count,'uncompressed_bytes':total,
                'all_member_crcs_verified':True,'directory':str(destroot)}
        manifest['projects'].append(record)
        save(bundle/'expansion.json',manifest)
        print(json.dumps(record),flush=True)
    manifest.update(state='completed',finished_at=now(),elapsed_seconds=time.monotonic()-start)
    save(bundle/'expansion.json',manifest)

if __name__=='__main__':
    main()
