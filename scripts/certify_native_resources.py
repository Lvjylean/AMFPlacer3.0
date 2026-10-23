#!/usr/bin/env python3
"""One-time immutable resource upgrade; no synthesis/placement/DCP writes."""
import argparse,json,os,subprocess,time
from pathlib import Path
from run_dfx import sha,save


def certify(native, query, output, binary, side_policy='interior-prohibit'):
    cache=Path(native['path']);old_contract=Path(native['contract']);q=Path(query);out=Path(output)
    if side_policy not in ('interior-prohibit','any-prohibit'):
        raise ValueError('Unknown side policy')
    if sha(cache)!=native['sha256'] or sha(old_contract)!=native['contract_sha256']:
        raise ValueError('Parent cache/contract hash mismatch')
    c=json.loads(old_contract.read_text());m=json.loads((q/'manifest.json').read_text())
    if c.get('state')!='sealed' or c['cache_sha256']!=native['sha256'] or c.get('resource_policy'):
        raise ValueError('Expected an immutable sealed parent without a resource policy')
    shell=c['static_shell_sha256']
    if (m.get('state')!='completed' or m.get('exit_code')!=0 or not m.get('read_only')
        or not m.get('input_unchanged') or m.get('input_sha256_before')!=shell or m.get('input_sha256_after')!=shell
        or sha(Path(m['input']))!=shell or sha(q/'probe.tcl')!=m['probe_sha256']):
        raise ValueError('Resource evidence does not certify the matching unchanged static shell')
    inventory=q/'resources.tsv';geometry=q/'regions.tsv'
    inventory_sha,geometry_sha=sha(inventory),sha(geometry)
    out.mkdir(parents=True,exist_ok=False)
    cfg={'Native cache action':'certify-resources','Native static cache proposal':str(cache),
         'Native static cache sealed output':str(out/'sealed.bin'),'Native static identity':c['identity'],
         'device':c['device'],'ClockPeriod':str(c['clock_period_ns']),
         'Native static shell sha256':shell,'Native static connectivity verified':'1','Native interfaces verified':'1',
         'Native resource inventory file':str(inventory),'Native resource geometry file':str(geometry),
         'Native resource inventory sha256':inventory_sha,'Native resource geometry sha256':geometry_sha,
         'Native resource side policy':side_policy}
    save(out/'config.json',cfg);start=time.monotonic()
    with (out/'certify.log').open('w') as f:
        rc=subprocess.run([str(binary),'--native-cache',str(out/'config.json')],stdout=f,stderr=subprocess.STDOUT).returncode
    record=dict(exit_code=rc,seconds=time.monotonic()-start,parent_cache_sha256=native['sha256'],
                parent_contract_sha256=native['contract_sha256'],query_manifest_sha256=sha(q/'manifest.json'),
                binary_sha256=sha(binary),inventory_sha256=inventory_sha,geometry_sha256=geometry_sha)
    save(out/'certification.json',record)
    if rc:raise RuntimeError('Native resource certification failed; preserve independent output')
    if (sha(cache)!=native['sha256'] or sha(old_contract)!=native['contract_sha256'] or
        sha(inventory)!=inventory_sha or sha(geometry)!=geometry_sha):
        raise ValueError('Certification inputs changed')
    child=dict(c,cache_sha256=sha(out/'sealed.bin'),parent_cache_sha256=native['sha256'],
               parent_contract_sha256=native['contract_sha256'],resource_certification=record,
               resource_policy=dict(version='clock-region-slice-dsp-v1',cache_format=3,side_policy=side_policy,
                                    inventory_sha256=inventory_sha,geometry_sha256=geometry_sha))
    child['evidence_hashes']=dict(c.get('evidence_hashes',{}),**{str(inventory):inventory_sha,str(geometry):geometry_sha})
    save(out/'contract.json',child)
    result={'path':str(out/'sealed.bin'),'sha256':child['cache_sha256'],
            'contract':str(out/'contract.json'),'contract_sha256':sha(out/'contract.json')}
    save(out/'native.json',result);return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--query',required=True)
    p.add_argument('--output',required=True);p.add_argument('--binary',required=True)
    p.add_argument('--side-policy',choices=['interior-prohibit','any-prohibit'],default='interior-prohibit')
    a=p.parse_args();print(json.dumps(certify(json.loads(Path(a.native).read_text()),a.query,a.output,a.binary,a.side_policy)))
