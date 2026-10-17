#!/usr/bin/env python3
"""Bounded immutable cache extension from already extracted access evidence.

No Vivado queries, routing, old module coordinates, or design-specific masks.
The hard policy is deliberately limited to completely blocked real FF Q exits.
"""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

POLICY = 'fixed-static-ff-q-v1'
HARD = 'ALL_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA'

def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()

def save(p,d):
    Path(p).write_text(json.dumps(d,indent=2,ensure_ascii=False)+'\n')

def serialized_identity(path):
    # Validate complete embedded archives and byte-identical untouched graph
    # without a second multi-million-cell C++ object expansion.
    import struct
    with Path(path).open('rb') as f:
        magic=f.read(11)
        if magic not in (b'AMFSTATIC3\n',b'AMFSTATIC4\n'):raise ValueError('Cache format')
        size,crc=struct.unpack('<QQ',f.read(16))
        if Path(path).stat().st_size!=size+27:raise ValueError('Incomplete cache write')
        sealed,count=struct.unpack('<QQ',f.read(16))
        if sealed!=1 or count>10000:raise ValueError('Cache metadata header')
        def string():
            n=struct.unpack('<Q',f.read(8))[0]
            if n>64*1024*1024:raise ValueError('Cache string bound')
            s=f.read(n)
            if len(s)!=n:raise ValueError('Truncated cache metadata')
            return s
        meta={}
        for _ in range(count):
            key=string().decode();value=string()
            if key in meta:raise ValueError('Duplicate cache metadata')
            meta[key]=hashlib.sha256(value).hexdigest()
        h=hashlib.sha256()
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
        return meta,h.hexdigest()

def worker(config,out):
    begin=time.monotonic(); stages={}; cfg=json.loads(Path(config).read_text())
    def tick(name,start):stages[name]=time.monotonic()-start
    t=time.monotonic()
    for p,h in cfg['input_hashes'].items():
        if sha(p)!=h:raise ValueError('Input identity changed: '+p)
    parent=json.loads(Path(cfg['parent_contract']).read_text())
    if sha(cfg['parent_cache'])!=parent['cache_sha256']:
        raise ValueError('Parent static cache mismatch')
    if sha(cfg['static_shell'])!=parent['static_shell_sha256']:
        raise ValueError('Static shell mismatch')
    coverage=json.loads(Path(cfg['archives']['coverage']).read_text())
    provenance=json.loads(Path(cfg['archives']['provenance']).read_text())
    if (coverage['status']!='FULL_SCOPE_INVENTORY_AND_FIRST_HOP_ANALYSIS_COMPLETE'
        or coverage['source_sha_unchanged'] is not True
        or coverage['source_dcp']['imported']['sha256']!=provenance['extraction_source_checkpoint_sha256']
        or provenance['static_shell_sha256']!=parent['static_shell_sha256']
        or provenance['full_reference_sha256']!=parent['full_reference_sha256']
        or provenance['static_routes_preserved_and_verified'] is not True):
        raise ValueError('Access evidence is not bound to this fixed static shell')
    tick('identity_and_source_verification',t)
    t=time.monotonic(); counts=collections.Counter(); masks=set()
    with gzip.open(cfg['archives']['bel_pin_access'],'rt') as f:
        header=next(f).rstrip('\n').split('\t')
        if header!=['module','site','bel','bel_type','bel_pin','direction','state','site_pins','site_pin_states']:
            raise ValueError('Unsupported complete classification schema')
        for line in f:
            r=line.rstrip('\n').split('\t')
            if len(r)!=9:raise ValueError('Malformed BEL-pin row')
            counts[r[0]]+=1
            if r[6]!=HARD or r[5]!='OUT' or not r[4].endswith('/Q'):continue
            bel=r[2].rsplit('/',1)[-1]
            if re.fullmatch('[A-H]FF2?',bel):
                key=(r[0],r[1],bel)
                if key in masks:raise ValueError('Duplicate hard FF Q row')
                masks.add(key)
    if dict(counts)!={m:c['bel_pins'] for m,c in coverage['coverage'].items()}:
        raise ValueError('Incomplete A/B BEL-pin classification')
    mask=out/'ff_q_mask.tsv'
    mask.write_text('module\tsite\tbel\n'+''.join('\t'.join(r)+'\n' for r in sorted(masks)))
    tick('complete_classification_read_and_mask',t)
    native={'Native cache action':'certify-pin-access',
            'Native static cache proposal':cfg['parent_cache'],
            'Native static cache sealed output':str(out/'sealed.bin'),
            'identity':parent['identity'],'device':parent['device'],
            'clock_period':str(parent['clock_period_ns']),
            'static_shell_sha256':parent['static_shell_sha256'],
            'policy':POLICY,'ff_q_mask_file':str(mask)}
    archive_hashes={}
    for key,p in cfg['archives'].items():
        h=sha(p);archive_hashes[key]=h
        if cfg['input_hashes'].get(p)!=h:raise ValueError('Unverified archive: '+key)
        native[key+'_file']=p;native[key+'_sha256']=h
    save(out/'native-certify.json',native)
    t=time.monotonic()
    with (out/'native-certify.log').open('w') as log:
        subprocess.run([cfg['binary'],'--native-cache',str(out/'native-certify.json')],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    tick('native_cache_load_extend_write',t)
    t=time.monotonic()
    before,base_graph=serialized_identity(cfg['parent_cache'])
    after,new_graph=serialized_identity(out/'sealed.bin')
    if base_graph!=new_graph or any(after.get(k)!=v for k,v in before.items()):
        raise ValueError('Static graph or existing metadata changed')
    for key,h in archive_hashes.items():
        if after.get('pin_archive/'+key)!=h:raise ValueError('Embedded classification differs: '+key)
    tick('complete_archive_and_unchanged_graph_readback',t)
    t=time.monotonic();child=dict(parent)
    child.update(cache_sha256=sha(out/'sealed.bin'),parent_cache_sha256=parent['cache_sha256'],
        cache_extension_binary_sha256=sha(cfg['binary']),
        pin_access_policy={'version':POLICY,'cache_format':4,'all_classifications_embedded':True,
            'hard_mask_rows':len(masks),'archive_sha256':archive_hashes,
            'limitations':'first-hop classification only; LUT/DSP partial, boundary-dependent and special access retained as evidence, not blanket BEL bans'},
        scope='static connectivity/placement/site occupancy plus complete SLICE/DSP first-hop classifications; hard FF Q filtering')
    save(out/'contract.json',child)
    tick('output_hash_and_contract',t)
    save(out/'generation.json',dict(state='completed',seconds=time.monotonic()-begin,stages=stages,
        parent_cache=cfg['parent_cache'],cache_sha256=child['cache_sha256'],
        contract_sha256=sha(out/'contract.json'),complete_pin_rows=dict(counts),
        hard_ff_rows=[list(r) for r in sorted(masks)],new_vivado_extraction=False,
        classification_source='previous complete extraction reused byte-exact',
        cold_new_shell_extraction_measured=False,existing_static_graph_byte_exact=True,embedded_archives_sha256_exact=True))

def run(config,out,budget):
    out=Path(out).resolve();out.mkdir(parents=True,exist_ok=False);begin=time.monotonic()
    p=subprocess.Popen([sys.executable,__file__,'--worker','--config',str(Path(config).resolve()),
                        '--output',str(out)],start_new_session=True)
    try:
        rc=p.wait(timeout=budget)
        if rc:raise RuntimeError('Cache generation failed, exit '+str(rc))
        result=json.loads((out/'generation.json').read_text())
        result.update(total_wall_seconds=time.monotonic()-begin,budget_seconds=budget)
        if result['total_wall_seconds']>budget:raise TimeoutError('Cache deadline exceeded')
        save(out/'generation.json',result)
        print(json.dumps(result,ensure_ascii=False))
    except BaseException as error:
        if p.poll() is None:
            os.killpg(p.pid,signal.SIGTERM)
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
        save(out/'generation-failed.json',dict(state='failed',error=repr(error),budget_seconds=budget,
            seconds=time.monotonic()-begin,partial_artifacts_not_usable=True))
        raise

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    p.add_argument('--budget-seconds',type=float,default=180);p.add_argument('--worker',action='store_true')
    a=p.parse_args()
    if a.worker:worker(a.config,Path(a.output))
    else:run(a.config,a.output,a.budget_seconds)
