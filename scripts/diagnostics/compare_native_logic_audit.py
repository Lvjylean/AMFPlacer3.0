#!/usr/bin/env python3
"""Compare every exported native parameter, pin and net without sampling."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import subprocess


def normalize(directory):
    if not (directory/'complete.tsv').exists():
        raise ValueError('Native export is not complete: '+str(directory))
    summary = {}
    for table in ['parameters','pins','nets','ports']:
        src = directory/(table+'.tsv')
        unsorted = directory/(table+'.canonical-unsorted.tsv')
        target = directory/(table+'.canonical.tsv')
        count = 0
        extras = collections.Counter()
        raw = hashlib.sha256()
        with src.open() as f, unsorted.open('w') as out:
            raw.update(next(f).encode())
            for line in f:
                raw.update(line.encode())
                row = line.rstrip('\n').split('\t')
                if table == 'parameters':
                    if len(row)!=4: raise ValueError('Malformed parameter row')
                    key = '\t'.join(row[:3]); value = row[3:]
                    if row[3]=='': extras['empty:'+row[1]+':'+row[2]]+=1
                else:
                    key = row[0]; value = row[1:]
                    if table=='nets':
                        value=sorted(value); extras['pin_memberships']+=len(value)
                    if table=='pins':
                        if len(row)!=5: raise ValueError('Malformed pin row')
                        if row[1] not in {'IN','OUT','INOUT'} or row[2] not in {'0','1'} or row[3] not in {'0','1'} or not row[4]:
                            raise ValueError('Missing or invalid native pin property: '+row[0])
                        extras['connected:'+row[2]]+=1; extras['inverted:'+row[3]]+=1
                digest = hashlib.sha256(json.dumps(value,separators=(',',':')).encode()).hexdigest()
                out.write(key+'\t'+digest+'\n'); count+=1
        subprocess.run(['sort','--parallel=2','-S','256M','-T',str(directory),'-o',str(target),str(unsorted)],env={**os.environ,'LC_ALL':'C'},check=True)
        # Delete only this script's intermediate sort input, retaining source and sorted ledger.
        unsorted.unlink()
        previous=None;canonical=hashlib.sha256();duplicates=0
        with target.open() as f:
            for line in f:
                key=line.rsplit('\t',1)[0]
                duplicates+=key==previous; previous=key
                canonical.update(line.encode())
        summary[table]=dict(rows=count,duplicate_keys=duplicates,raw_sha256=raw.hexdigest(),canonical_sha256=canonical.hexdigest(),details=dict(extras))
        if duplicates: raise ValueError(f'Duplicate {table} keys: {duplicates}')
        print(table,count,canonical.hexdigest(),flush=True)
    (directory/'canonical-summary.json').write_text(json.dumps(summary,indent=2))


def compare(left,right,output):
    result={}
    for table in ['parameters','pins','nets','ports']:
        counts=collections.Counter();examples=[]
        with (left/(table+'.canonical.tsv')).open() as a,(right/(table+'.canonical.tsv')).open() as b:
            x=next(a,None);y=next(b,None)
            while x is not None or y is not None:
                kx=x.rsplit('\t',1)[0] if x else None
                ky=y.rsplit('\t',1)[0] if y else None
                if kx==ky:
                    kind='equal' if x==y else 'changed';key=kx
                    x=next(a,None);y=next(b,None)
                elif ky is None or (kx is not None and kx<ky):
                    kind='removed';key=kx;x=next(a,None)
                else:
                    kind='added';key=ky;y=next(b,None)
                counts[kind]+=1
                if kind!='equal' and len(examples)<20:examples.append(dict(change=kind,key=key))
        result[table]=dict(counts=dict(counts),examples=examples,pass_exact=not any(counts[k] for k in ['changed','added','removed']))
        print(table,result[table],flush=True)
    result['all_tables_identical']=all(v['pass_exact'] for v in result.values())
    result['scope']='observable exported values and topology only; blank protected parameters remain unproved'
    result['functional_equivalence_proven_by_this_comparator']=False
    output.write_text(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest='cmd',required=True)
    p=sub.add_parser('normalize');p.add_argument('directory',type=Path)
    p=sub.add_parser('compare');p.add_argument('left',type=Path);p.add_argument('right',type=Path);p.add_argument('output',type=Path)
    a=parser.parse_args()
    if a.cmd=='normalize':normalize(a.directory)
    else:compare(a.left,a.right,a.output)
