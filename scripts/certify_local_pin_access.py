#!/usr/bin/env python3
"""Build an immutable local-access child cache within a caller-owned deadline."""
import argparse,copy,hashlib,json,os,subprocess,time
from pathlib import Path
from native_local_pin_policy import compile_policy,VERSION
import struct,gzip
from run_dfx import save,sha

def serialized_identity_with_sha(path):
    # One bounded read provides both the full-file hash and unchanged-graph hash.
    full=hashlib.sha256()
    with Path(path).open('rb') as f:
        def read(n):
            b=f.read(n)
            if len(b)!=n:raise ValueError('Truncated cache')
            full.update(b);return b
        magic=read(11)
        if magic not in (b'AMFSTATIC3\n',b'AMFSTATIC4\n'):raise ValueError('Cache version')
        size,crc=struct.unpack('<QQ',read(16))
        if Path(path).stat().st_size!=size+27:raise ValueError('Cache length')
        sealed,count=struct.unpack('<QQ',read(16))
        if sealed!=1 or count>10000:raise ValueError('Cache metadata')
        def string():
            n=struct.unpack('<Q',read(8))[0]
            if n>64*1024*1024:raise ValueError('Metadata string bound')
            return read(n)
        meta={}
        for _ in range(count):
            key=string().decode();value=string()
            if key in meta:raise ValueError('Duplicate metadata')
            meta[key]=hashlib.sha256(value).hexdigest()
        graph=hashlib.sha256()
        while True:
            b=f.read(4*1024*1024)
            if not b:break
            graph.update(b);full.update(b)
    return meta,graph.hexdigest(),full.hexdigest()

def run(config,output,deadline):
    cfg=json.loads(Path(config).read_text());out=Path(output);out.mkdir(exist_ok=False)
    started=time.monotonic();stages={}
    def remaining():
        value=deadline-time.monotonic()
        if value<=0:raise TimeoutError('Added local-cache budget exceeded')
        return value
    t=time.monotonic()
    for p,h in cfg['input_hashes'].items():
        if sha(p)!=h:raise ValueError('Immutable cache input changed: '+p)
        remaining()
    parent=json.loads(Path(cfg['parent_contract']).read_text())
    before,graph_before,parent_sha=serialized_identity_with_sha(cfg['parent_cache'])
    if parent_sha!=parent['cache_sha256'] or sha(cfg['static_shell'])!=parent['static_shell_sha256']:
        raise ValueError('Parent shell/cache identity mismatch')
    stages['identity_seconds']=time.monotonic()-t
    t=time.monotonic()
    save(out/'progress.json',dict(stage='identity_complete',stages=stages))
    compiled=compile_policy(cfg['topology_root'],cfg['occupancy'],cfg['owners'],out/'policy',remaining(),cfg.get('owner_marker'),cfg.get('session_token'))
    stages['compile_seconds']=time.monotonic()-t;remaining()
    save(out/'progress.json',dict(stage='policy_complete',stages=stages))
    t=time.monotonic()
    policy=out/'policy/local_access.tsv.gz'
    policy.write_bytes(gzip.compress((out/'policy/local_access.tsv').read_bytes(),compresslevel=1,mtime=0))
    stages['lossless_compression_seconds']=time.monotonic()-t;remaining()
    spec={'Native cache action':'certify-local-pin-access','Native static cache proposal':cfg['parent_cache'],
          'Native static cache sealed output':str(out/'sealed.bin'),'identity':parent['identity'],
          'device':parent['device'],'clock_period':str(cfg['clock_period']),
          'static_shell_sha256':parent['static_shell_sha256'],'local_policy':VERSION,
          'local_access_file':str(policy),'local_access_sha256':sha(policy),
          'local_owner_sha256':sha(cfg['owners'])}
    save(out/'extend.json',spec)
    t=time.monotonic()
    with (out/'extend.log').open('w') as log:
        subprocess.run([cfg['binary'],'--native-cache',str(out/'extend.json')],stdout=log,
                       stderr=subprocess.STDOUT,check=True,timeout=remaining())
    stages['cache_load_extend_write_seconds']=time.monotonic()-t
    t=time.monotonic()
    after,graph_after,child_sha=serialized_identity_with_sha(out/'sealed.bin')
    added={'local_pin_access_policy':VERSION.encode(),'local_pin_access_data':policy.read_bytes(),
           'local_pin_access_sha256':spec['local_access_sha256'].encode(),
           'local_pin_owner_sha256':spec['local_owner_sha256'].encode()}
    expected=dict(before,**{k:hashlib.sha256(v).hexdigest() for k,v in added.items()})
    if graph_before!=graph_after or after!=expected:raise ValueError('Child cache changed existing graph or certificate')
    stages['cache_readback_seconds']=time.monotonic()-t
    contract=copy.deepcopy(parent)
    contract.update(cache_sha256=child_sha,
                    parent_cache={'path':cfg['parent_cache'],'sha256':parent['cache_sha256'],
                                  'contract':cfg['parent_contract'],'contract_sha256':sha(cfg['parent_contract'])},
                    extension_binary_sha256=sha(cfg['binary']),
                    local_pin_access_policy={'version':VERSION,'data_sha256':spec['local_access_sha256'],
                                             'owner_sha256':spec['local_owner_sha256'],
                                             'same_routed_shell':True,'graph_and_old_metadata_byte_exact':True,
                                             'first_hop_only':True,'global_route_required':True})
    # producer_binary_sha256 continues to identify V0-71/72, not this extension.
    save(out/'contract.json',contract)
    remaining()
    result={'state':'completed','stages':stages,'seconds':time.monotonic()-started,'compiled':compiled,
            'native_static_cache':{'path':str(out/'sealed.bin'),'sha256':contract['cache_sha256'],
                                  'contract':str(out/'contract.json'),'contract_sha256':sha(out/'contract.json')}}
    save(out/'generation.json',result);remaining()
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    p.add_argument('--deadline',required=True,type=float);a=p.parse_args()
    try:run(a.config,a.output,a.deadline)
    except Exception as e:
        out=Path(a.output)
        if out.exists():save(out/'failure.json',dict(error=repr(e),failed_at=time.time()))
        raise

