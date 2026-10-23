#!/usr/bin/env python3
"""Find an identity-matched checkpoint without modifying AMF inputs or checkpoints."""
import argparse
import json
from pathlib import Path
import time
from audit_amf2_cases import run_one
from prepare_amf2_cases import SOURCES, now, save, sha

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root,bundle,audit,out=[p.resolve() for p in [args.root,args.bundle,args.audit,args.output]]
    if not out.is_relative_to(root/'experiments/preflight'):
        raise RuntimeError('Expected project preflight directory')
    out.mkdir(parents=True,exist_ok=False)
    manifest={'started_at':now(),'state':'running','script_sha256':sha(Path(__file__)),
              'placement_executed':False,'input_modified':False}
    save(out/'manifest.json',manifest)
    pending=[s[0] for s in SOURCES if s[3]]
    results=[]
    deadline=time.monotonic()+7200
    while pending:
        progress=False
        for name in list(pending):
            original=audit/name/'result.json'
            if not original.exists(): continue
            result=json.loads(original.read_text())
            if result['state']=='running': continue
            case=json.loads((bundle/'cases'/name/'case.json').read_text())
            attempts=[{'report':str(original),'state':result['state'],'identity':result.get('identity')}]
            chosen=None
            if result.get('identity',{}).get('exact_match'):
                chosen={'dcp':result['source_dcp'],'sha256':result['input_sha256'],'audit':str(original)}
            else:
                candidates=[x for x in case['dcp_candidates'] if x['path']!=result['source_dcp']]
                def order(c):
                    p=c['path'].lower()
                    return (0 if p.endswith('_placed.dcp') else 1 if p.endswith('_routed.dcp') else 2,p)
                for index,candidate in enumerate(sorted(candidates,key=order)):
                    trial=dict(case,selected_dcp=candidate)
                    trial_root=out/('candidate-'+str(index+1))
                    trial_root.mkdir(exist_ok=True)
                    trial_result=run_one(root,trial,bundle,trial_root)
                    report=trial_root/name/'result.json'
                    attempts.append({'report':str(report),'state':trial_result['state'],'identity':trial_result.get('identity')})
                    if trial_result.get('identity',{}).get('exact_match'):
                        chosen={'dcp':candidate['path'],'sha256':candidate['sha256'],'audit':str(report)}
                        break
            record={'case':name,'state':'identity_matched' if chosen else 'needs_consistent_reexport',
                    'chosen':chosen,'attempts':attempts,'connectivity_equivalence_checked':False,
                    'u250_ready':False,'placement_executed':False}
            save(bundle/'cases'/name/'backend_pair.json',record)
            results.append(record);pending.remove(name);progress=True
            save(out/'summary.json',results)
            print(json.dumps(record,ensure_ascii=False),flush=True)
        if not progress:
            if time.monotonic()>deadline: raise RuntimeError('Waiting limit exceeded')
            time.sleep(5)
    manifest.update(state='completed',finished_at=now(),identity_matched=sum(r['chosen'] is not None for r in results))
    save(out/'manifest.json',manifest)
    print(json.dumps(manifest,indent=2),flush=True)

if __name__=='__main__':
    main()
